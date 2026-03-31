#include "oneshot_native/NotificationManager.h"

#include "oneshot_native/CommandProtocol.h"
#include "oneshot_native/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <commctrl.h>
#include <windowsx.h>

namespace
{
    constexpr int kNotificationMargin = 16;
    constexpr int kNotificationGap = 10;
    constexpr UINT_PTR kPulseTimerId = 1;
    constexpr UINT_PTR kHoverPropValue = 1;
    constexpr int kThumbControlId = 100;
    constexpr int kMarkupButtonId = 101;
    constexpr int kDismissButtonId = 102;
    constexpr int kDragThreshold = 4;
    constexpr int kPulseFrameCount = 10;
    constexpr int kPulseTimerMs = 32;
    constexpr wchar_t kHoverProp[] = L"OneShot.NotificationHover";
    constexpr wchar_t kCloseButtonLabel[] = L"Close";
    constexpr wchar_t kMarkupButtonLabel[] = L"Markup";
    constexpr wchar_t kCloseGlyph[] = L"\uE711";
    constexpr wchar_t kMarkupGlyph[] = L"\uE70F";

    struct NotificationLayout
    {
        int dpi{96};
        int windowWidth{332};
        int windowHeight{160};
        int borderThickness{1};
        int cardRadius{22};
        int thumbnailRadius{18};
        int buttonRadius{14};
        int thumbnailInset{2};
        RECT cardRect{};
        RECT thumbnailRect{};
        RECT dismissButtonRect{};
        RECT markupButtonRect{};
    };

    RECT MakeRect(int left, int top, int width, int height)
    {
        return RECT{ left, top, left + width, top + height };
    }

    bool IsHovering(HWND hwnd)
    {
        return GetPropW(hwnd, kHoverProp) != nullptr;
    }

    void SetHover(HWND hwnd, bool hover)
    {
        if (hover)
        {
            SetPropW(hwnd, kHoverProp, reinterpret_cast<HANDLE>(kHoverPropValue));
        }
        else
        {
            RemovePropW(hwnd, kHoverProp);
        }
    }

    int ScaleValue(int value, int dpi)
    {
        return oneshot::ui::ScaleForDpi(value, static_cast<UINT>(dpi <= 0 ? 96 : dpi));
    }

    NotificationLayout BuildLayout(int dpi)
    {
        NotificationLayout layout{};
        layout.dpi = dpi > 0 ? dpi : 96;
        layout.windowWidth = ScaleValue(332, layout.dpi);
        layout.windowHeight = ScaleValue(160, layout.dpi);
        layout.borderThickness = ScaleValue(1, layout.dpi);
        layout.cardRadius = ScaleValue(22, layout.dpi);
        layout.thumbnailRadius = ScaleValue(18, layout.dpi);
        layout.buttonRadius = ScaleValue(14, layout.dpi);
        layout.thumbnailInset = std::max(1, ScaleValue(2, layout.dpi));

        layout.cardRect = MakeRect(0, 0, layout.windowWidth, layout.windowHeight);

        const int outerPadding = ScaleValue(12, layout.dpi);
        const int columnGap = ScaleValue(10, layout.dpi);
        const int actionWidth = ScaleValue(56, layout.dpi);
        const int dismissSize = ScaleValue(56, layout.dpi);
        const int markupSize = ScaleValue(56, layout.dpi);
        const int contentHeight = (layout.cardRect.bottom - layout.cardRect.top) - (outerPadding * 2);
        const int contentWidth = (layout.cardRect.right - layout.cardRect.left) - (outerPadding * 2);
        const int thumbWidth = contentWidth - actionWidth - columnGap;
        const int thumbHeight = contentHeight;
        const int actionLeft = layout.cardRect.right - outerPadding - actionWidth;
        const int actionTop = layout.cardRect.top + outerPadding;

        layout.thumbnailRect = MakeRect(layout.cardRect.left + outerPadding, layout.cardRect.top + outerPadding, thumbWidth, thumbHeight);
        layout.dismissButtonRect = MakeRect(actionLeft, actionTop, dismissSize, dismissSize);
        layout.markupButtonRect = MakeRect(actionLeft, layout.cardRect.bottom - outerPadding - markupSize, markupSize, markupSize);
        return layout;
    }

    RECT GetPrimaryWorkArea()
    {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        return workArea;
    }

    COLORREF ComputePulseColor(COLORREF base, COLORREF pulse, int pulseFrame)
    {
        if (pulseFrame <= 0 || pulseFrame >= kPulseFrameCount)
        {
            return base;
        }

        const int midpoint = kPulseFrameCount / 2;
        const int distance = pulseFrame <= midpoint ? pulseFrame : (kPulseFrameCount - pulseFrame);
        const BYTE alpha = static_cast<BYTE>(std::min(170, (distance * 170) / std::max(1, midpoint)));
        return oneshot::ui::BlendColor(base, pulse, alpha);
    }

    void TrackHover(HWND hwnd)
    {
        if (IsHovering(hwnd))
        {
            return;
        }

        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        SetHover(hwnd, true);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void TraceNotificationFailure(const wchar_t* step, DWORD error)
    {
        wchar_t buffer[256]{};
        swprintf_s(buffer, L"OneShot notification: %ls failed (error=%lu)\n", step, static_cast<unsigned long>(error));
        OutputDebugStringW(buffer);
    }

    void TraceNotificationVisibility(HWND hwnd, const RECT& rect)
    {
        wchar_t buffer[320]{};
        swprintf_s(
            buffer,
            L"OneShot notification: hwnd=%p visible=%d rect=(%ld,%ld)-(%ld,%ld)\n",
            hwnd,
            IsWindowVisible(hwnd) ? 1 : 0,
            static_cast<long>(rect.left),
            static_cast<long>(rect.top),
            static_cast<long>(rect.right),
            static_cast<long>(rect.bottom));
        OutputDebugStringW(buffer);
    }

    std::wstring DescribeWindowsError(DWORD error)
    {
        if (error == 0)
        {
            return L"";
        }

        LPWSTR message = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            reinterpret_cast<LPWSTR>(&message),
            0,
            nullptr);
        if (length == 0 || !message)
        {
            return L"Unknown Win32 error";
        }

        std::wstring result(message, length);
        LocalFree(message);

        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        {
            result.pop_back();
        }

        return result;
    }

    HFONT CreateSymbolFont(HWND hwnd, int pointSize)
    {
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        const int height = -MulDiv(pointSize, static_cast<int>(dpi == 0 ? 96 : dpi), 72);
        return CreateFontW(
            height,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe MDL2 Assets");
    }

    RECT InsetRectCopy(const RECT& rect, int inset)
    {
        RECT result = rect;
        InflateRect(&result, -inset, -inset);
        return result;
    }

    RECT ComputeAspectFitRect(const RECT& bounds, int sourceWidth, int sourceHeight)
    {
        const int boundsWidth = bounds.right - bounds.left;
        const int boundsHeight = bounds.bottom - bounds.top;
        if (boundsWidth <= 0 || boundsHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        {
            return RECT{};
        }

        const double scale = std::min(
            static_cast<double>(boundsWidth) / static_cast<double>(sourceWidth),
            static_cast<double>(boundsHeight) / static_cast<double>(sourceHeight));
        const int drawWidth = std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceWidth) * scale)));
        const int drawHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(sourceHeight) * scale)));
        const int left = bounds.left + ((boundsWidth - drawWidth) / 2);
        const int top = bounds.top + ((boundsHeight - drawHeight) / 2);
        return RECT{ left, top, left + drawWidth, top + drawHeight };
    }
}

namespace oneshot
{
    struct NotificationManager::NotificationWindow
    {
        NotificationManager* manager{nullptr};
        HWND hwnd{nullptr};
        HWND thumbnail{nullptr};
        HWND markupButton{nullptr};
        HWND dismissButton{nullptr};
        HWND tooltip{nullptr};
        CapturedImage image;
        std::filesystem::path savedPath;
        std::filesystem::path dragPath;
        HFONT uiFont{nullptr};
        HFONT uiBoldFont{nullptr};
        POINT dragAnchor{};
        bool pointerDown{false};
        bool dragInProgress{false};
        COLORREF baseBackgroundColor{ui::GetPalette().panelBackground};
        COLORREF pulseBackgroundColor{ui::GetPalette().accent};
        COLORREF backgroundColor{ui::GetPalette().panelBackground};
        int pulseFrame{0};
        NotificationLayout layout{};
    };

    static void ApplyNotificationRegions(NotificationManager::NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        if (notification->hwnd)
        {
            ui::ApplyRoundedWindowRegion(notification->hwnd, notification->layout.cardRadius);
        }
        if (notification->thumbnail)
        {
            ui::ApplyRoundedWindowRegion(notification->thumbnail, notification->layout.thumbnailRadius);
        }
        if (notification->dismissButton)
        {
            ui::ApplyRoundedWindowRegion(notification->dismissButton, notification->layout.buttonRadius);
        }
        if (notification->markupButton)
        {
            ui::ApplyRoundedWindowRegion(notification->markupButton, notification->layout.buttonRadius);
        }
    }

    static void ConfigureTooltips(NotificationManager::NotificationWindow* notification)
    {
        if (!notification || !notification->tooltip)
        {
            return;
        }

        const auto addTool = [notification](HWND toolWindow, const wchar_t* text)
        {
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = notification->hwnd;
            tool.uId = reinterpret_cast<UINT_PTR>(toolWindow);
            tool.lpszText = const_cast<LPWSTR>(text);
            SendMessageW(notification->tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        };

        addTool(notification->dismissButton, kCloseButtonLabel);
        addTool(notification->markupButton, kMarkupButtonLabel);
    }

    static void ReleaseThumbnailPointerCapture(NotificationManager::NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        if (GetCapture() == notification->thumbnail)
        {
            ReleaseCapture();
        }
    }

    static void ResetThumbnailDragState(NotificationManager::NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        notification->pointerDown = false;
        notification->dragInProgress = false;
        ReleaseThumbnailPointerCapture(notification);
    }

    static void ConfigureButton(HWND button, HFONT font)
    {
        if (!button)
        {
            return;
        }

        LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
        style &= ~BS_TYPEMASK;
        style |= BS_OWNERDRAW;
        SetWindowLongPtrW(button, GWL_STYLE, style);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    static void ApplyLayout(NotificationManager::NotificationWindow* notification)
    {
        if (!notification || !notification->hwnd)
        {
            return;
        }

        const UINT dpi = GetDpiForWindow(notification->hwnd);
        notification->layout = BuildLayout(static_cast<int>(dpi));

        const auto& layout = notification->layout;
        MoveWindow(
            notification->thumbnail,
            layout.thumbnailRect.left,
            layout.thumbnailRect.top,
            layout.thumbnailRect.right - layout.thumbnailRect.left,
            layout.thumbnailRect.bottom - layout.thumbnailRect.top,
            TRUE);
        MoveWindow(
            notification->dismissButton,
            layout.dismissButtonRect.left,
            layout.dismissButtonRect.top,
            layout.dismissButtonRect.right - layout.dismissButtonRect.left,
            layout.dismissButtonRect.bottom - layout.dismissButtonRect.top,
            TRUE);
        MoveWindow(
            notification->markupButton,
            layout.markupButtonRect.left,
            layout.markupButtonRect.top,
            layout.markupButtonRect.right - layout.markupButtonRect.left,
            layout.markupButtonRect.bottom - layout.markupButtonRect.top,
            TRUE);
        ApplyNotificationRegions(notification);
        InvalidateRect(notification->thumbnail, nullptr, TRUE);
        InvalidateRect(notification->hwnd, nullptr, TRUE);
    }

    static void BringNotificationToTopmostFront(NotificationManager::NotificationWindow* notification)
    {
        if (!notification || !notification->hwnd || !IsWindow(notification->hwnd))
        {
            return;
        }

        SetWindowPos(
            notification->hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(notification->hwnd);
        SetActiveWindow(notification->hwnd);
        SetForegroundWindow(notification->hwnd);
    }

    static void DrawNotificationButton(const NotificationManager::NotificationWindow& notification, const DRAWITEMSTRUCT& draw)
    {
        const auto& palette = ui::GetPalette();
        const bool hovered = IsHovering(draw.hwndItem) || (draw.itemState & ODS_HOTLIGHT) != 0;
        const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
        const bool focused = (draw.itemState & ODS_FOCUS) != 0;
        const bool dismiss = draw.CtlID == kDismissButtonId;
        RECT clientRect{};
        GetClientRect(draw.hwndItem, &clientRect);

        COLORREF fill = dismiss ? palette.dangerBackground : palette.accent;
        COLORREF text = palette.text;

        if (!dismiss)
        {
            fill = hovered ? palette.accentHot : palette.accent;
            if (pressed)
            {
                fill = palette.accentPressed;
            }
        }
        else
        {
            if (hovered)
            {
                fill = palette.dangerBackgroundHot;
            }
            if (pressed)
            {
                fill = palette.dangerBackgroundPressed;
            }
            text = RGB(251, 239, 244);
        }

        HBRUSH backgroundBrush = CreateSolidBrush(notification.backgroundColor);
        FillRect(draw.hDC, &clientRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        ui::FillRoundedRect(draw.hDC, clientRect, fill, notification.layout.buttonRadius);

        HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(draw.hDC, font) : nullptr;
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, text);
        RECT textRect = clientRect;
        const wchar_t* glyph = draw.CtlID == kDismissButtonId ? kCloseGlyph : kMarkupGlyph;
        DrawTextW(draw.hDC, glyph, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (oldFont)
        {
            SelectObject(draw.hDC, oldFont);
        }
        if (focused)
        {
            RECT focusRect = clientRect;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(draw.hDC, &focusRect);
        }
    }

    static LRESULT CALLBACK NotificationButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            if (!dc)
            {
                return TRUE;
            }

            auto* notification = reinterpret_cast<NotificationManager::NotificationWindow*>(
                GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
            RECT client{};
            GetClientRect(hwnd, &client);
            const COLORREF color = notification ? notification->backgroundColor : ui::GetPalette().panelBackground;
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &client, brush);
            DeleteObject(brush);
            return TRUE;
        }
        case WM_MOUSEMOVE:
            TrackHover(hwnd);
            break;
        case WM_MOUSELEAVE:
            SetHover(hwnd, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, NotificationButtonProc, 0);
            RemovePropW(hwnd, kHoverProp);
            break;
        default:
            break;
        }

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK NotificationThumbnailProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* notification = reinterpret_cast<NotificationManager::NotificationWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            notification = static_cast<NotificationManager::NotificationWindow*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(notification));
            return TRUE;
        }

        if (!notification)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_MOUSEMOVE:
            TrackHover(hwnd);
            if (notification->pointerDown && !notification->dragInProgress && (wParam & MK_LBUTTON))
            {
                const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (abs(point.x - notification->dragAnchor.x) >= kDragThreshold || abs(point.y - notification->dragAnchor.y) >= kDragThreshold)
                {
                    notification->dragInProgress = true;
                    notification->pointerDown = false;
                    ReleaseThumbnailPointerCapture(notification);
                    notification->manager->StartDrag(notification, hwnd);
                    ResetThumbnailDragState(notification);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            SetHover(hwnd, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_CAPTURECHANGED:
            ResetThumbnailDragState(notification);
            return 0;
        case WM_LBUTTONDOWN:
            notification->pointerDown = true;
            notification->dragInProgress = false;
            notification->dragAnchor = POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            ResetThumbnailDragState(notification);
            return 0;
        case WM_PAINT:
        {
            const auto& palette = ui::GetPalette();
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);

            RECT client{};
            GetClientRect(hwnd, &client);
            const COLORREF borderColor = IsHovering(hwnd) ? palette.accentHot : palette.panelBorder;
            ui::FillRoundedRect(dc, client, RGB(16, 22, 30), notification->layout.thumbnailRadius);
            ui::FrameRoundedRect(dc, client, borderColor, notification->layout.thumbnailRadius);

            if (notification->image.bitmap)
            {
                BITMAP bitmap{};
                if (GetObjectW(notification->image.bitmap, sizeof(bitmap), &bitmap) == sizeof(bitmap))
                {
                    HDC memoryDc = CreateCompatibleDC(dc);
                    HGDIOBJ oldBitmap = SelectObject(memoryDc, notification->image.bitmap);
                    const RECT contentRect = InsetRectCopy(client, notification->layout.thumbnailInset);
                    const RECT imageRect = ComputeAspectFitRect(contentRect, bitmap.bmWidth, bitmap.bmHeight);
                    SetStretchBltMode(dc, HALFTONE);
                    SetBrushOrgEx(dc, 0, 0, nullptr);
                    StretchBlt(
                        dc,
                        imageRect.left,
                        imageRect.top,
                        imageRect.right - imageRect.left,
                        imageRect.bottom - imageRect.top,
                        memoryDc,
                        0,
                        0,
                        bitmap.bmWidth,
                        bitmap.bmHeight,
                        SRCCOPY);
                    SelectObject(memoryDc, oldBitmap);
                    DeleteDC(memoryDc);
                }
            }

            EndPaint(hwnd, &paint);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK NotificationWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* notification = reinterpret_cast<NotificationManager::NotificationWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            notification = static_cast<NotificationManager::NotificationWindow*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(notification));
            return TRUE;
        }

        if (!notification)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_ERASEBKGND:
            return TRUE;
        case WM_TIMER:
            if (wParam == kPulseTimerId)
            {
                notification->pulseFrame++;
                notification->backgroundColor = ComputePulseColor(notification->baseBackgroundColor, notification->pulseBackgroundColor, notification->pulseFrame);
                InvalidateRect(hwnd, nullptr, FALSE);
                if (notification->pulseFrame >= kPulseFrameCount)
                {
                    KillTimer(hwnd, kPulseTimerId);
                    notification->pulseFrame = 0;
                    notification->backgroundColor = notification->baseBackgroundColor;
                }
                return 0;
            }
            break;
        case WM_DRAWITEM:
        {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (!draw)
            {
                break;
            }

            DrawNotificationButton(*notification, *draw);
            return TRUE;
        }
        case WM_SIZE:
            ApplyNotificationRegions(notification);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kMarkupButtonId:
                notification->manager->OpenMarkup(notification);
                return 0;
            case kDismissButtonId:
                notification->manager->CloseNotification(notification);
                return 0;
            default:
                break;
            }
            break;
        case WM_DPICHANGED:
        {
            const auto* suggestedRect = reinterpret_cast<RECT*>(lParam);
            notification->layout = BuildLayout(HIWORD(wParam));
            SetWindowPos(
                hwnd,
                nullptr,
                suggestedRect->left,
                suggestedRect->top,
                notification->layout.windowWidth,
                notification->layout.windowHeight,
                SWP_NOACTIVATE | SWP_NOZORDER);
            if (notification->uiFont)
            {
                DeleteObject(notification->uiFont);
                notification->uiFont = CreateSymbolFont(hwnd, 12);
                SendMessageW(notification->dismissButton, WM_SETFONT, reinterpret_cast<WPARAM>(notification->uiFont), TRUE);
            }
            if (notification->uiBoldFont)
            {
                DeleteObject(notification->uiBoldFont);
                notification->uiBoldFont = CreateSymbolFont(hwnd, 16);
                SendMessageW(notification->markupButton, WM_SETFONT, reinterpret_cast<WPARAM>(notification->uiBoldFont), TRUE);
            }
            ApplyLayout(notification);
            return 0;
        }
        case WM_PAINT:
        {
            const auto& palette = ui::GetPalette();
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);

            ui::FillRoundedRect(dc, notification->layout.cardRect, notification->backgroundColor, notification->layout.cardRadius);
            ui::FrameRoundedRect(dc, notification->layout.cardRect, palette.panelBorder, notification->layout.cardRadius, notification->layout.borderThickness);

            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kPulseTimerId);
            ResetThumbnailDragState(notification);
            if (notification->tooltip)
            {
                DestroyWindow(notification->tooltip);
                notification->tooltip = nullptr;
            }
            if (notification->uiFont)
            {
                DeleteObject(notification->uiFont);
                notification->uiFont = nullptr;
            }
            if (notification->uiBoldFont)
            {
                DeleteObject(notification->uiBoldFont);
                notification->uiBoldFont = nullptr;
            }
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    NotificationManager::NotificationManager(AppPaths paths, OutputService& outputService)
        : _paths(std::move(paths))
        , _markupEditorSettings(_paths)
        , _markupEditor(outputService, _markupEditorSettings)
        , _outputService(outputService)
    {
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
        InitCommonControlsEx(&controls);

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = NotificationWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OneShotNative.Notification";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&windowClass);

        WNDCLASSW thumbnailClass{};
        thumbnailClass.lpfnWndProc = NotificationThumbnailProc;
        thumbnailClass.hInstance = GetModuleHandleW(nullptr);
        thumbnailClass.lpszClassName = L"OneShotNative.NotificationThumbnail";
        thumbnailClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
        RegisterClassW(&thumbnailClass);
    }

    NotificationManager::~NotificationManager()
    {
        CloseAll();
    }

    void NotificationManager::Show(HWND owner, CapturedImage image, const std::filesystem::path& savedPath, const std::filesystem::path& dragPath)
    {
        (void)owner;

        _debugState = NotificationDebugState{};
        _debugState.lastAttemptUtc = CurrentIso8601Utc();
        _debugState.showAttempted = true;

        const auto recordFailure = [this](const wchar_t* step, DWORD error)
        {
            _debugState.lastFailedStep = step ? step : L"";
            _debugState.lastErrorCode = error;
            _debugState.lastErrorText = DescribeWindowsError(error);
            TraceNotificationFailure(step ? step : L"notification", error);
        };

        auto notification = std::make_unique<NotificationWindow>();
        notification->manager = this;
        notification->image = std::move(image);
        notification->savedPath = savedPath;
        notification->dragPath = dragPath;
        notification->layout = BuildLayout(96);

        notification->hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"OneShotNative.Notification",
            kAppName,
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            notification->layout.windowWidth,
            notification->layout.windowHeight,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            notification.get());

        if (!notification->hwnd)
        {
            recordFailure(L"CreateWindowExW(notification)", GetLastError());
            return;
        }
        _debugState.windowCreated = true;

        notification->uiFont = CreateSymbolFont(notification->hwnd, 12);
        notification->uiBoldFont = CreateSymbolFont(notification->hwnd, 16);
        notification->layout = BuildLayout(static_cast<int>(GetDpiForWindow(notification->hwnd)));
        notification->thumbnail = CreateWindowExW(
            0,
            L"OneShotNative.NotificationThumbnail",
            L"",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThumbControlId)),
            GetModuleHandleW(nullptr),
            notification.get());
        if (!notification->thumbnail)
        {
            recordFailure(L"CreateWindowExW(thumbnail)", GetLastError());
            DestroyWindow(notification->hwnd);
            return;
        }
        _debugState.thumbnailCreated = true;
        notification->dismissButton = CreateWindowExW(
            0,
            WC_BUTTONW,
            kCloseButtonLabel,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDismissButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);
        if (!notification->dismissButton)
        {
            recordFailure(L"CreateWindowExW(dismissButton)", GetLastError());
            DestroyWindow(notification->hwnd);
            return;
        }
        _debugState.dismissButtonCreated = true;
        notification->markupButton = CreateWindowExW(
            0,
            WC_BUTTONW,
            kMarkupButtonLabel,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkupButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);
        if (!notification->markupButton)
        {
            recordFailure(L"CreateWindowExW(markupButton)", GetLastError());
            DestroyWindow(notification->hwnd);
            return;
        }
        _debugState.markupButtonCreated = true;

        notification->tooltip = CreateWindowExW(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSW,
            nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            notification->hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        ConfigureButton(notification->dismissButton, notification->uiFont);
        ConfigureButton(notification->markupButton, notification->uiBoldFont);
        SetWindowSubclass(notification->dismissButton, NotificationButtonProc, 0, 0);
        SetWindowSubclass(notification->markupButton, NotificationButtonProc, 0, 0);
        ConfigureTooltips(notification.get());

        ApplyLayout(notification.get());

        _notifications.insert(_notifications.begin(), std::move(notification));
        RepositionAll();
        _debugState.showWindowResult = ShowWindow(_notifications.front()->hwnd, SW_SHOWNOACTIVATE) ? 1 : 0;
        UpdateWindow(_notifications.front()->hwnd);
        RedrawWindow(_notifications.front()->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

        RECT windowRect{};
        if (GetWindowRect(_notifications.front()->hwnd, &windowRect))
        {
            _debugState.windowRect = windowRect;
            _debugState.windowVisible = IsWindowVisible(_notifications.front()->hwnd) != FALSE;
            TraceNotificationVisibility(_notifications.front()->hwnd, windowRect);
            if (!_debugState.windowVisible && _debugState.lastFailedStep.empty())
            {
                _debugState.lastFailedStep = L"visibility-check";
                _debugState.lastErrorText = L"Notification window was created but is not visible after show.";
            }
        }
        else
        {
            recordFailure(L"GetWindowRect(notification)", GetLastError());
        }
    }

    void NotificationManager::CloseAll()
    {
        while (!_notifications.empty())
        {
            CloseNotification(_notifications.front().get());
        }
    }

    void NotificationManager::StartDrag(NotificationWindow* notification, HWND sourceWindow)
    {
        if (!notification || !notification->hwnd || !IsWindow(notification->hwnd))
        {
            return;
        }

        if (notification->dragPath.empty() || !std::filesystem::exists(notification->dragPath))
        {
            return;
        }

        std::wstring error;
        const bool started = _dragDrop.StartFileDrag(sourceWindow, notification->dragPath, error);
        RepositionAll();
        if (!started && !error.empty())
        {
            MessageBoxW(notification->hwnd, error.c_str(), kAppName, MB_OK | MB_ICONWARNING);
        }
    }

    void NotificationManager::OpenMarkup(NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        auto updatedImage = _markupEditor.Edit(notification->hwnd, notification->image);
        if (!updatedImage.has_value())
        {
            return;
        }

        std::wstring error;
        if (!_outputService.SavePng(*updatedImage, notification->savedPath, error))
        {
            MessageBoxW(notification->hwnd, error.c_str(), kAppName, MB_OK | MB_ICONERROR);
            return;
        }

        std::wstring dragError;
        if (!_outputService.SavePng(*updatedImage, notification->dragPath, dragError))
        {
            MessageBoxW(notification->hwnd, dragError.c_str(), kAppName, MB_OK | MB_ICONERROR);
            return;
        }

        notification->image = std::move(*updatedImage);
        InvalidateRect(notification->thumbnail, nullptr, TRUE);
        KillTimer(notification->hwnd, kPulseTimerId);
        notification->pulseFrame = 0;
        notification->backgroundColor = notification->baseBackgroundColor;
        SetTimer(notification->hwnd, kPulseTimerId, kPulseTimerMs, nullptr);
        InvalidateRect(notification->hwnd, nullptr, FALSE);
    }

    void NotificationManager::RepositionAll()
    {
        const RECT workArea = GetPrimaryWorkArea();
        int top = workArea.top + kNotificationMargin;
        HWND insertAfter = HWND_TOPMOST;

        for (auto& notification : _notifications)
        {
            if (!notification->hwnd)
            {
                continue;
            }

            const int width = notification->layout.windowWidth;
            const int height = notification->layout.windowHeight;
            const int left = workArea.right - kNotificationMargin - width;
            if (!SetWindowPos(notification->hwnd, insertAfter, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW))
            {
                const DWORD error = GetLastError();
                _debugState.lastFailedStep = L"SetWindowPos(notification)";
                _debugState.lastErrorCode = error;
                _debugState.lastErrorText = DescribeWindowsError(error);
                TraceNotificationFailure(L"SetWindowPos(notification)", error);
            }
            else
            {
                insertAfter = notification->hwnd;
            }
            top += height + kNotificationGap;
        }
    }

    void NotificationManager::CloseNotification(NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        auto it = std::find_if(_notifications.begin(), _notifications.end(), [notification](const auto& entry) { return entry.get() == notification; });
        if (it == _notifications.end())
        {
            return;
        }

        if ((*it)->hwnd)
        {
            DestroyWindow((*it)->hwnd);
            (*it)->hwnd = nullptr;
        }

        std::error_code error;
        std::filesystem::remove((*it)->dragPath, error);
        _notifications.erase(it);
        RepositionAll();
    }
}
