#include "oneshot_native/TrayController.h"

#include <strsafe.h>

namespace
{
    UINT_PTR AnchorMenuId(oneshot::NotificationAnchor anchor)
    {
        switch (anchor)
        {
        case oneshot::NotificationAnchor::TopLeft:
            return oneshot::kTrayMenuNotificationAnchorTopLeft;
        case oneshot::NotificationAnchor::BottomLeft:
            return oneshot::kTrayMenuNotificationAnchorBottomLeft;
        case oneshot::NotificationAnchor::TopRight:
            return oneshot::kTrayMenuNotificationAnchorTopRight;
        case oneshot::NotificationAnchor::BottomRight:
            return oneshot::kTrayMenuNotificationAnchorBottomRight;
        default:
            return oneshot::kTrayMenuNotificationAnchorTopRight;
        }
    }

    UINT_PTR GrowDirectionMenuId(oneshot::NotificationGrowDirection growDirection)
    {
        switch (growDirection)
        {
        case oneshot::NotificationGrowDirection::Left:
            return oneshot::kTrayMenuNotificationGrowLeft;
        case oneshot::NotificationGrowDirection::Right:
            return oneshot::kTrayMenuNotificationGrowRight;
        case oneshot::NotificationGrowDirection::Up:
            return oneshot::kTrayMenuNotificationGrowUp;
        case oneshot::NotificationGrowDirection::Down:
            return oneshot::kTrayMenuNotificationGrowDown;
        default:
            return oneshot::kTrayMenuNotificationGrowDown;
        }
    }

    const wchar_t* AnchorMenuLabel(oneshot::NotificationAnchor anchor)
    {
        switch (anchor)
        {
        case oneshot::NotificationAnchor::TopLeft:
            return L"Top Left";
        case oneshot::NotificationAnchor::BottomLeft:
            return L"Bottom Left";
        case oneshot::NotificationAnchor::TopRight:
            return L"Top Right";
        case oneshot::NotificationAnchor::BottomRight:
            return L"Bottom Right";
        default:
            return L"Top Right";
        }
    }

    const wchar_t* GrowDirectionMenuLabel(oneshot::NotificationGrowDirection growDirection)
    {
        switch (growDirection)
        {
        case oneshot::NotificationGrowDirection::Left:
            return L"Left";
        case oneshot::NotificationGrowDirection::Right:
            return L"Right";
        case oneshot::NotificationGrowDirection::Up:
            return L"Up";
        case oneshot::NotificationGrowDirection::Down:
            return L"Down";
        default:
            return L"Down";
        }
    }
}

namespace oneshot
{
    TrayController::~TrayController()
    {
        Dispose();
    }

    bool TrayController::Initialize(HWND hwnd)
    {
        _iconData = {};
        _iconData.cbSize = sizeof(_iconData);
        _iconData.hWnd = hwnd;
        _iconData.uID = kTrayIconId;
        _iconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        _iconData.uCallbackMessage = kWindowMessageTrayIcon;
        _iconData.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        StringCchCopyW(_iconData.szTip, ARRAYSIZE(_iconData.szTip), kAppName);

        _initialized = Shell_NotifyIconW(NIM_ADD, &_iconData) == TRUE;
        return _initialized;
    }

    void TrayController::Restore(HWND hwnd)
    {
        Dispose();
        (void)Initialize(hwnd);
    }

    void TrayController::Dispose()
    {
        if (!_initialized)
        {
            return;
        }

        Shell_NotifyIconW(NIM_DELETE, &_iconData);
        _initialized = false;
    }

    void TrayController::ShowContextMenu(HWND hwnd, const NotificationPlacement& placement) const
    {
        HMENU menu = CreatePopupMenu();
        HMENU anchorMenu = CreatePopupMenu();
        HMENU growMenu = CreatePopupMenu();

        const NotificationAnchor anchors[] =
        {
            NotificationAnchor::TopLeft,
            NotificationAnchor::BottomLeft,
            NotificationAnchor::TopRight,
            NotificationAnchor::BottomRight
        };
        for (const NotificationAnchor anchor : anchors)
        {
            UINT flags = MF_STRING;
            if (placement.anchor == anchor)
            {
                flags |= MF_CHECKED;
            }

            AppendMenuW(anchorMenu, flags, AnchorMenuId(anchor), AnchorMenuLabel(anchor));
        }

        for (const NotificationGrowDirection growDirection : ValidGrowDirections(placement.anchor))
        {
            UINT flags = MF_STRING;
            if (placement.growDirection == growDirection)
            {
                flags |= MF_CHECKED;
            }

            AppendMenuW(growMenu, flags, GrowDirectionMenuId(growDirection), GrowDirectionMenuLabel(growDirection));
        }

        AppendMenuW(menu, MF_STRING, kTrayMenuSnapshot, L"Take Snapshot");
        AppendMenuW(menu, MF_STRING, kTrayMenuDiagnostics, L"Diagnostics");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(anchorMenu), L"Anchor");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(growMenu), L"Grow Direction");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"Exit");

        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
    }

    void TrayController::ShowBalloon(std::wstring_view title, std::wstring_view text) const
    {
        if (!_initialized)
        {
            return;
        }

        auto iconData = _iconData;
        iconData.uFlags = NIF_INFO;
        StringCchCopyW(iconData.szInfoTitle, ARRAYSIZE(iconData.szInfoTitle), title.data());
        StringCchCopyW(iconData.szInfo, ARRAYSIZE(iconData.szInfo), text.data());
        iconData.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &iconData);
    }
}
