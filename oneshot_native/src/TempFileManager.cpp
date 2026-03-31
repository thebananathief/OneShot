#include "oneshot_native/TempFileManager.h"

namespace oneshot
{
    TempFileManager::TempFileManager(AppPaths paths)
        : _paths(std::move(paths))
    {
    }

    void TempFileManager::Initialize() const
    {
        std::filesystem::create_directories(_paths.tempDirectory);
        std::filesystem::create_directories(_paths.logDirectory);
        std::filesystem::create_directories(_paths.screenshotsDirectory);
    }

    const AppPaths& TempFileManager::Paths() const noexcept
    {
        return _paths;
    }
}
