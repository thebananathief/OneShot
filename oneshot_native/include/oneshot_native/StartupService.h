#pragma once

#include "oneshot_native/PathService.h"

namespace oneshot
{
    class StartupService
    {
    public:
        explicit StartupService(AppPaths paths);

        bool Install(std::wstring& message) const;
        bool Uninstall(std::wstring& message) const;
        [[nodiscard]] bool IsEnabled() const;

    private:
        AppPaths _paths;
    };
}
