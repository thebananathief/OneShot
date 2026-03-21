#include "oneshot_native/TrayController.h"

#include <strsafe.h>

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

    void TrayController::Dispose()
    {
        if (!_initialized)
        {
            return;
        }

        Shell_NotifyIconW(NIM_DELETE, &_iconData);
        _initialized = false;
    }

    void TrayController::ShowContextMenu(HWND hwnd) const
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kTrayMenuSnapshot, L"Take Snapshot");
        AppendMenuW(menu, MF_STRING, kTrayMenuDiagnostics, L"Diagnostics");
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
