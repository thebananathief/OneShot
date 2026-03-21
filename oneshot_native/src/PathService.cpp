#include "oneshot_native/PathService.h"

#include <shlobj.h>

namespace
{
    std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID id)
    {
        PWSTR rawPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &rawPath)))
        {
            return {};
        }

        std::filesystem::path result(rawPath);
        CoTaskMemFree(rawPath);
        return result;
    }
}

namespace oneshot
{
    AppPaths ResolveAppPaths()
    {
        AppPaths paths{};
        paths.localAppDataRoot = GetKnownFolder(FOLDERID_LocalAppData) / L"OneShot";
        paths.tempDirectory = paths.localAppDataRoot / L"Temp";
        paths.logDirectory = paths.localAppDataRoot / L"Logs";
        paths.screenshotsDirectory = GetKnownFolder(FOLDERID_Pictures) / L"Screenshots";

        std::wstring executableBuffer(MAX_PATH, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executableBuffer.data(), static_cast<DWORD>(executableBuffer.size()));
        executableBuffer.resize(length);
        paths.executablePath = executableBuffer;

        return paths;
    }

    std::wstring ToWideString(const std::filesystem::path& path)
    {
        return path.wstring();
    }
}
