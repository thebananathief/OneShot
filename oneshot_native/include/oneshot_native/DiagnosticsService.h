#pragma once

#include "oneshot_native/NotificationManager.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    class DiagnosticsService
    {
    public:
        explicit DiagnosticsService(AppPaths paths);

        [[nodiscard]] std::wstring BuildDiagnosticsText(bool startupEnabled, bool snapshotActive, const NotificationDebugState& notificationDebug) const;

    private:
        AppPaths _paths;
    };
}
