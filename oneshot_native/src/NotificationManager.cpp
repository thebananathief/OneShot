#include "oneshot_native/NotificationManager.h"

#include <commctrl.h>
#include <windowsx.h>

namespace
{
    constexpr int kNotificationMargin = 16;
    constexpr int kNotificationGap = 8;
    constexpr UINT_PTR kPulseTimerId = 1;
    constexpr int kThumbControlId = 100;
    constexpr int kMarkupButtonId = 101;
    constexpr int kDismissButtonId = 102;
    constexpr int kDragThreshold = 4;
    constexpr COLORREF kBorderColor = RGB(128, 128, 128);
    constexpr COLORREF kThumbBorderColor = RGB(160, 160, 160);

    struct NotificationLayout
    {
        int dpi{96};
        int borderThickness{1};
        int outerPadding{4};
        int columnGap{4};
        int buttonSize{48};
        int cardWidth{258};
        int cardHeight{119};
        RECT thumbnailRect{};
        RECT dismissButtonRect{};
        RECT markupButtonRect{};
    };

    int ScaleValue(int value, int dpi)
    {
        return MulDiv(value, dpi, 96);
    }

    RECT MakeRect(int left, int top, int width, int height)
    {
        return RECT{ left, top, left + width, top + height };
    }

    NotificationLayout BuildLayout(int dpi)
    {
        NotificationLayout layout{};
        layout.dpi = dpi > 0 ? dpi : 96;
        layout.borderThickness = ScaleValue(1, layout.dpi);
        layout.outerPadding = ScaleValue(4, layout.dpi);
        layout.columnGap = ScaleValue(4, layout.dpi);
        layout.buttonSize = ScaleValue(48, layout.dpi);
        layout.cardWidth = ScaleValue(258, layout.dpi);
        layout.cardHeight = ScaleValue(119, layout.dpi);

        const int contentLeft = layout.outerPadding;
        const int contentTop = layout.outerPadding;
        const int contentWidth = layout.cardWidth - (layout.outerPadding * 2);
        const int contentHeight = layout.cardHeight - (layout.outerPadding * 2);
        const int thumbnailWidth = contentWidth - layout.buttonSize - layout.columnGap;
        const int thumbnailHeight = contentHeight;
        const int buttonLeft = contentLeft + thumbnailWidth + layout.columnGap;

        layout.thumbnailRect = MakeRect(contentLeft, contentTop, thumbnailWidth, thumbnailHeight);
        layout.dismissButtonRect = MakeRect(buttonLeft, contentTop, layout.buttonSize, layout.buttonSize);
        layout.markupButtonRect = MakeRect(buttonLeft, contentTop + contentHeight - layout.buttonSize, layout.buttonSize, layout.buttonSize);
        return layout;
    }

    RECT GetPrimaryWorkArea()
    {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        return workArea;
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
        POINT dragAnchor{};
        bool pointerDown{false};
        bool dragInProgress{false};
        COLORREF baseBackgroundColor{RGB(45, 45, 48)};
        COLORREF pulseBackgroundColor{RGB(61, 121, 216)};
        COLORREF backgroundColor{RGB(45, 45, 48)};
        HBRUSH backgroundBrush{nullptr};
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
        case WM_MOUSEMOVE:
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
        case WM_LBUTTONUP:
            ResetThumbnailDragState(notification);
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);

            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH fillBrush = CreateSolidBrush(notification->baseBackgroundColor);
            FillRect(dc, &client, fillBrush);
            DeleteObject(fillBrush);
            HBRUSH borderBrush = CreateSolidBrush(kThumbBorderColor);
            FrameRect(dc, &client, borderBrush);
            DeleteObject(borderBrush);

            if (notification->thumbnailBitmap)
            {
                BITMAP bitmap{};
                if (GetObjectW(notification->thumbnailBitmap, sizeof(bitmap), &bitmap) == sizeof(bitmap))
                {
                    HDC memoryDc = CreateCompatibleDC(dc);
                    HGDIOBJ oldBitmap = SelectObject(memoryDc, notification->thumbnailBitmap);
                    const int contentLeft = notification->layout.borderThickness;
                    const int contentTop = notification->layout.borderThickness;
                    const int contentWidth = std::max<int>(0, (client.right - client.left) - (notification->layout.borderThickness * 2));
                    const int contentHeight = std::max<int>(0, (client.bottom - client.top) - (notification->layout.borderThickness * 2));
                    SetStretchBltMode(dc, HALFTONE);
                    StretchBlt(dc, contentLeft, contentTop, contentWidth, contentHeight, memoryDc, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
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
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
        {
            auto dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(240, 240, 240));
            return reinterpret_cast<INT_PTR>(notification->backgroundBrush);
        }
        case WM_TIMER:
            if (wParam == kPulseTimerId)
            {
                notification->pulseFrame++;
                notification->backgroundColor = notification->pulseFrame == 1 ? notification->pulseBackgroundColor : notification->baseBackgroundColor;
                if (notification->backgroundBrush)
                {
                    DeleteObject(notification->backgroundBrush);
                }
                notification->backgroundBrush = CreateSolidBrush(notification->backgroundColor);
                InvalidateRect(hwnd, nullptr, TRUE);
                UpdateWindow(hwnd);

                if (notification->pulseFrame >= 2)
                {
                    KillTimer(hwnd, kPulseTimerId);
                    notification->pulseFrame = 0;
                }
                return 0;
            }
            break;
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
                suggestedRect->right - suggestedRect->left,
                suggestedRect->bottom - suggestedRect->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            ApplyLayout(notification);
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);

            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, notification->backgroundBrush);

            HPEN pen = CreatePen(PS_SOLID, notification->layout.borderThickness, kBorderColor);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, client.left, client.top, client.right, client.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);

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
            if (notification->backgroundBrush)
            {
                DeleteObject(notification->backgroundBrush);
                notification->backgroundBrush = nullptr;
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
        notification->backgroundBrush = CreateSolidBrush(notification->backgroundColor);
        notification->layout = BuildLayout(96);

        notification->hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"OneShotNative.Notification",
            kAppName,
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            notification->layout.cardWidth,
            notification->layout.cardHeight,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            notification.get());

        if (!notification->hwnd)
        {
            return;
        }

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
            L"X",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            notification->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkupButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);

        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(notification->dismissButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(notification->markupButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

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
        if (notification->backgroundBrush)
        {
            DeleteObject(notification->backgroundBrush);
        }
        notification->backgroundBrush = CreateSolidBrush(notification->backgroundColor);
        SetTimer(notification->hwnd, kPulseTimerId, 250, nullptr);
        InvalidateRect(notification->hwnd, nullptr, TRUE);
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

            const int width = notification->layout.cardWidth;
            const int height = notification->layout.cardHeight;
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
