#pragma once

#include "oneshot_native/Common.h"

namespace oneshot
{
    struct AppPaths
    {
        std::filesystem::path localAppDataRoot;
        std::filesystem::path tempDirectory;
        std::filesystem::path logDirectory;
        std::filesystem::path screenshotsDirectory;
        std::filesystem::path executablePath;
    };

    AppPaths ResolveAppPaths();
    std::wstring ToWideString(const std::filesystem::path& path);
}
