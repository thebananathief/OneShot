#include "oneshot_native/TempFileManager.h"

#include <algorithm>

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

        for (const auto& entry : std::filesystem::directory_iterator(_paths.tempDirectory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto filename = entry.path().filename().wstring();
            if (filename.rfind(L"drag_", 0) == 0 && entry.path().extension() == L".png")
            {
                std::error_code error;
                std::filesystem::remove(entry.path(), error);
            }
        }
    }

    std::filesystem::path TempFileManager::CreateDragImagePath() const
    {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return _paths.tempDirectory / L"drag_fallback.png";
        }

        wchar_t guidBuffer[64]{};
        StringFromGUID2(guid, guidBuffer, static_cast<int>(std::size(guidBuffer)));
        std::wstring filename = guidBuffer;
        filename.erase(std::remove(filename.begin(), filename.end(), L'{'), filename.end());
        filename.erase(std::remove(filename.begin(), filename.end(), L'}'), filename.end());
        std::replace(filename.begin(), filename.end(), L'-', L'_');
        return _paths.tempDirectory / (L"drag_" + filename + L".png");
    }

    const AppPaths& TempFileManager::Paths() const noexcept
    {
        return _paths;
    }
}
