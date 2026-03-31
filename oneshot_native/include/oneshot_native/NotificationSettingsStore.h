#pragma once

#include "oneshot_native/NotificationPlacement.h"
#include "oneshot_native/PathService.h"

namespace oneshot
{
    class NotificationSettingsStore
    {
    public:
        explicit NotificationSettingsStore(const AppPaths& paths);

        [[nodiscard]] const NotificationPlacement& GetPlacement() const noexcept;
        void SavePlacement(NotificationPlacement placement) noexcept;

    private:
        [[nodiscard]] NotificationPlacement Load() const noexcept;
        void PersistToDisk(NotificationPlacement placement) const noexcept;

        std::filesystem::path _settingsPath;
        NotificationPlacement _placement;
    };
}
