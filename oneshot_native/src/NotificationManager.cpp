#include "oneshot_native/NotificationManager.h"

#include <commctrl.h>
#include <windowsx.h>

namespace
{
    constexpr int kNotificationWidth = 340;
    constexpr int kNotificationHeight = 124;
    constexpr int kNotificationMargin = 16;
    constexpr int kNotificationGap = 8;
    constexpr UINT_PTR kPulseTimerId = 1;
    constexpr int kThumbControlId = 100;
    constexpr int kMarkupButtonId = 101;
    constexpr int kDismissButtonId = 102;

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
        HWND titleLabel{nullptr};
        HWND pathLabel{nullptr};
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
    };

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
        {
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wParam), &client, notification->backgroundBrush);
            return TRUE;
        }
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
        case WM_LBUTTONDOWN:
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT thumbRect{};
            GetWindowRect(notification->thumbnail, &thumbRect);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&thumbRect), 2);
            if (PtInRect(&thumbRect, point))
            {
                notification->pointerDown = true;
                notification->dragAnchor = point;
                SetCapture(hwnd);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (notification->pointerDown && !notification->dragInProgress && (wParam & MK_LBUTTON))
            {
                POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (abs(point.x - notification->dragAnchor.x) >= 4 || abs(point.y - notification->dragAnchor.y) >= 4)
                {
                    notification->dragInProgress = true;
                    notification->manager->StartDrag(notification);
                    notification->dragInProgress = false;
                    notification->pointerDown = false;
                    ReleaseCapture();
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (notification->pointerDown)
            {
                notification->pointerDown = false;
                ReleaseCapture();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kPulseTimerId);
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
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&windowClass);
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

        notification->hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"OneShotNative.Notification",
            kAppName,
            WS_POPUPWINDOW | WS_CAPTION,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kNotificationWidth,
            kNotificationHeight,
            owner,
            nullptr,
            GetModuleHandleW(nullptr),
            notification.get());

        if (!notification->hwnd)
        {
            return;
        }

        notification->thumbnail = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE, 12, 12, 96, 72, notification->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kThumbControlId)), GetModuleHandleW(nullptr), nullptr);
        notification->titleLabel = CreateWindowExW(0, WC_STATICW, L"Screenshot copied and saved", WS_CHILD | WS_VISIBLE, 120, 12, 200, 20, notification->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        notification->pathLabel = CreateWindowExW(0, WC_STATICW, savedPath.filename().c_str(), WS_CHILD | WS_VISIBLE, 120, 36, 200, 20, notification->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        notification->markupButton = CreateWindowExW(0, WC_BUTTONW, L"Markup", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 72, 88, 28, notification->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkupButtonId)), GetModuleHandleW(nullptr), nullptr);
        notification->dismissButton = CreateWindowExW(0, WC_BUTTONW, L"Dismiss", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 216, 72, 88, 28, notification->hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDismissButtonId)), GetModuleHandleW(nullptr), nullptr);

        notification->thumbnailBitmap = static_cast<HBITMAP>(CopyImage(notification->image.bitmap, IMAGE_BITMAP, 96, 72, LR_CREATEDIBSECTION));
        SendMessageW(notification->thumbnail, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(notification->thumbnailBitmap));
        SendMessageW(notification->titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(notification->pathLabel, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        ShowWindow(notification->hwnd, SW_SHOW);
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
        if (notification->thumbnailBitmap)
        {
            DeleteObject(notification->thumbnailBitmap);
            notification->thumbnailBitmap = nullptr;
        }

        notification->thumbnailBitmap = static_cast<HBITMAP>(CopyImage(notification->image.bitmap, IMAGE_BITMAP, 96, 72, LR_CREATEDIBSECTION));
        SendMessageW(notification->thumbnail, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(notification->thumbnailBitmap));
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

            const int left = workArea.right - kNotificationMargin - kNotificationWidth;
            SetWindowPos(notification->hwnd, HWND_TOPMOST, left, top, kNotificationWidth, kNotificationHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            top += kNotificationHeight + kNotificationGap;
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
