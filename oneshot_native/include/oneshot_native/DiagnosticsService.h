#pragma once

#include "oneshot_native/PathService.h"

namespace oneshot
{
    class DiagnosticsService
    {
    public:
        explicit DiagnosticsService(AppPaths paths);

        [[nodiscard]] std::wstring BuildDiagnosticsText(bool startupEnabled) const;

    private:
        AppPaths _paths;
    };
}
