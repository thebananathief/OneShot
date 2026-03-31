#pragma once

#include "oneshot_native/Common.h"
#include "oneshot_native/NotificationPlacement.h"

namespace oneshot
{
    class TrayController
    {
    public:
        TrayController() = default;
        ~TrayController();

        bool Initialize(HWND hwnd);
        void Dispose();
        void Restore(HWND hwnd);
        void ShowContextMenu(HWND hwnd, const NotificationPlacement& placement) const;
        void ShowBalloon(std::wstring_view title, std::wstring_view text) const;
        [[nodiscard]] UINT ExplorerRestartMessage() const noexcept { return _explorerRestartMessage; }

    private:
        NOTIFYICONDATAW _iconData{};
        bool _initialized{false};
        UINT _explorerRestartMessage{RegisterWindowMessageW(L"TaskbarCreated")};
    };
}
