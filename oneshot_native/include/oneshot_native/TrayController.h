#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    class TrayController
    {
    public:
        TrayController() = default;
        ~TrayController();

        bool Initialize(HWND hwnd);
        void Dispose();
        void ShowContextMenu(HWND hwnd) const;
        void ShowBalloon(std::wstring_view title, std::wstring_view text) const;

    private:
        NOTIFYICONDATAW _iconData{};
        bool _initialized{false};
    };
}
