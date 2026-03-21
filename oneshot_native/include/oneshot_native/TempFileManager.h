#pragma once

#include "oneshot_native/PathService.h"

namespace oneshot
{
    class TempFileManager
    {
    public:
        explicit TempFileManager(AppPaths paths);

        void Initialize() const;
        [[nodiscard]] std::filesystem::path CreateDragImagePath() const;
        [[nodiscard]] const AppPaths& Paths() const noexcept;

    private:
        AppPaths _paths;
    };
}
