#include "oneshot_native/NotificationManager.h"

#include "oneshot_native/UiTheme.h"

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

    struct NotificationLayout
    {
        int dpi{96};
        int windowWidth{332};
        int windowHeight{160};
        int shadowOffsetX{4};
        int shadowOffsetY{6};
        int borderThickness{1};
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
        layout.shadowOffsetX = ScaleValue(4, layout.dpi);
        layout.shadowOffsetY = ScaleValue(6, layout.dpi);
        layout.borderThickness = ScaleValue(1, layout.dpi);

        const int cardLeft = ScaleValue(3, layout.dpi);
        const int cardTop = ScaleValue(3, layout.dpi);
        const int cardWidth = layout.windowWidth - layout.shadowOffsetX - ScaleValue(5, layout.dpi);
        const int cardHeight = layout.windowHeight - layout.shadowOffsetY - ScaleValue(5, layout.dpi);
        layout.cardRect = MakeRect(cardLeft, cardTop, cardWidth, cardHeight);

        const int outerPadding = ScaleValue(12, layout.dpi);
        const int columnGap = ScaleValue(10, layout.dpi);
        const int actionWidth = ScaleValue(84, layout.dpi);
        const int dismissHeight = ScaleValue(36, layout.dpi);
        const int markupHeight = ScaleValue(44, layout.dpi);
        const int contentHeight = (layout.cardRect.bottom - layout.cardRect.top) - (outerPadding * 2);
        const int contentWidth = (layout.cardRect.right - layout.cardRect.left) - (outerPadding * 2);
        const int thumbWidth = contentWidth - actionWidth - columnGap;
        const int thumbHeight = contentHeight;
        const int actionLeft = layout.cardRect.right - outerPadding - actionWidth;
        const int actionTop = layout.cardRect.top + outerPadding;

        layout.thumbnailRect = MakeRect(layout.cardRect.left + outerPadding, layout.cardRect.top + outerPadding, thumbWidth, thumbHeight);
        layout.dismissButtonRect = MakeRect(actionLeft, actionTop, actionWidth, dismissHeight);
        layout.markupButtonRect = MakeRect(actionLeft, layout.cardRect.bottom - outerPadding - markupHeight, actionWidth, markupHeight);
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
        CapturedImage image;
        std::filesystem::path savedPath;
        std::filesystem::path dragPath;
        HBITMAP thumbnailBitmap{nullptr};
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

    static void ResetThumbnailDragState(NotificationManager::NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        notification->pointerDown = false;
        notification->dragInProgress = false;
        if (GetCapture() == notification->thumbnail)
        {
            ReleaseCapture();
        }
    }

    static void RefreshThumbnailBitmap(NotificationManager::NotificationWindow* notification)
    {
        if (!notification || !notification->thumbnail)
        {
            return;
        }

        if (notification->thumbnailBitmap)
        {
            DeleteObject(notification->thumbnailBitmap);
            notification->thumbnailBitmap = nullptr;
        }

        const int thumbWidth = notification->layout.thumbnailRect.right - notification->layout.thumbnailRect.left;
        const int thumbHeight = notification->layout.thumbnailRect.bottom - notification->layout.thumbnailRect.top;
        notification->thumbnailBitmap = static_cast<HBITMAP>(CopyImage(
            notification->image.bitmap,
            IMAGE_BITMAP,
            thumbWidth,
            thumbHeight,
            LR_CREATEDIBSECTION));
        InvalidateRect(notification->thumbnail, nullptr, TRUE);
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
        RefreshThumbnailBitmap(notification);
        InvalidateRect(notification->hwnd, nullptr, TRUE);
    }

    static void DrawNotificationButton(const NotificationManager::NotificationWindow& notification, const DRAWITEMSTRUCT& draw)
    {
        const auto& palette = ui::GetPalette();
        const bool hovered = IsHovering(draw.hwndItem) || (draw.itemState & ODS_HOTLIGHT) != 0;
        const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
        const bool focused = (draw.itemState & ODS_FOCUS) != 0;
        const bool dismiss = draw.CtlID == kDismissButtonId;

        COLORREF fill = dismiss ? palette.dangerBackground : palette.accent;
        COLORREF border = dismiss ? palette.dangerBackgroundHot : palette.accentHot;
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

        ui::FillRoundedRect(draw.hDC, draw.rcItem, fill, 14);
        ui::FrameRoundedRect(draw.hDC, draw.rcItem, border, 14);

        wchar_t label[32]{};
        GetWindowTextW(draw.hwndItem, label, static_cast<int>(std::size(label)));
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(draw.hDC, font) : nullptr;
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, text);
        RECT textRect = draw.rcItem;
        DrawTextW(draw.hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (oldFont)
        {
            SelectObject(draw.hDC, oldFont);
        }
        if (focused)
        {
            RECT focusRect = draw.rcItem;
            InflateRect(&focusRect, -4, -4);
            DrawFocusRect(draw.hDC, &focusRect);
        }
    }

    static LRESULT CALLBACK NotificationButtonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
    {
        switch (message)
        {
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
                    notification->manager->StartDrag(notification);
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
            ui::FillRoundedRect(dc, client, RGB(16, 22, 30), 18);
            ui::FrameRoundedRect(dc, client, borderColor, 18);

            if (notification->thumbnailBitmap)
            {
                BITMAP bitmap{};
                if (GetObjectW(notification->thumbnailBitmap, sizeof(bitmap), &bitmap) == sizeof(bitmap))
                {
                    HDC memoryDc = CreateCompatibleDC(dc);
                    HGDIOBJ oldBitmap = SelectObject(memoryDc, notification->thumbnailBitmap);
                    const int inset = ScaleValue(1, notification->layout.dpi);
                    const int contentWidth = std::max<int>(0, static_cast<int>(client.right - client.left) - (inset * 2));
                    const int contentHeight = std::max<int>(0, static_cast<int>(client.bottom - client.top) - (inset * 2));
                    SetStretchBltMode(dc, HALFTONE);
                    StretchBlt(dc, inset, inset, contentWidth, contentHeight, memoryDc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
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
        case WM_CREATE:
            ui::ApplyModernFrame(hwnd, true);
            return 0;
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
                notification->uiFont = ui::CreateUiFont(hwnd, 10);
                SendMessageW(notification->dismissButton, WM_SETFONT, reinterpret_cast<WPARAM>(notification->uiFont), TRUE);
            }
            if (notification->uiBoldFont)
            {
                DeleteObject(notification->uiBoldFont);
                notification->uiBoldFont = ui::CreateUiFont(hwnd, 10, FW_SEMIBOLD);
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

            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH clearBrush = CreateSolidBrush(palette.windowBackground);
            FillRect(dc, &client, clearBrush);
            DeleteObject(clearBrush);

            RECT shadowRect = notification->layout.cardRect;
            OffsetRect(&shadowRect, notification->layout.shadowOffsetX, notification->layout.shadowOffsetY);
            ui::FillRoundedRect(dc, shadowRect, RGB(10, 14, 21), 22);
            ui::FillRoundedRect(dc, notification->layout.cardRect, notification->backgroundColor, 22);
            ui::FrameRoundedRect(dc, notification->layout.cardRect, palette.panelBorder, 22, notification->layout.borderThickness);

            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kPulseTimerId);
            ResetThumbnailDragState(notification);
            if (notification->thumbnailBitmap)
            {
                DeleteObject(notification->thumbnailBitmap);
                notification->thumbnailBitmap = nullptr;
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
        , _markupEditor(outputService)
        , _outputService(outputService)
    {
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
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            notification.get());

        if (!notification->hwnd)
        {
            return;
        }

        notification->uiFont = ui::CreateUiFont(notification->hwnd, 10);
        notification->uiBoldFont = ui::CreateUiFont(notification->hwnd, 10, FW_SEMIBOLD);
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
        notification->dismissButton = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDismissButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);
        notification->markupButton = CreateWindowExW(
            0,
            WC_BUTTONW,
            L"Markup",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkupButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);

        ConfigureButton(notification->dismissButton, notification->uiFont);
        ConfigureButton(notification->markupButton, notification->uiBoldFont);
        SetWindowSubclass(notification->dismissButton, NotificationButtonProc, 0, 0);
        SetWindowSubclass(notification->markupButton, NotificationButtonProc, 0, 0);

        ApplyLayout(notification.get());

        ShowWindow(notification->hwnd, SW_SHOWNOACTIVATE);
        _notifications.insert(_notifications.begin(), std::move(notification));
        RepositionAll();
    }

    void NotificationManager::CloseAll()
    {
        while (!_notifications.empty())
        {
            CloseNotification(_notifications.front().get());
        }
    }

    void NotificationManager::StartDrag(NotificationWindow* notification)
    {
        if (!notification)
        {
            return;
        }

        (void)_dragDrop.StartFileDrag(notification->hwnd, notification->dragPath);
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
        RefreshThumbnailBitmap(notification);
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

        for (auto& notification : _notifications)
        {
            if (!notification->hwnd)
            {
                continue;
            }

            const int width = notification->layout.windowWidth;
            const int height = notification->layout.windowHeight;
            const int left = workArea.right - kNotificationMargin - width;
            SetWindowPos(notification->hwnd, HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
