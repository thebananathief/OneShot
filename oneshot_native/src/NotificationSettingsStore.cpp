#include "oneshot_native/NotificationSettingsStore.h"

namespace
{
    constexpr wchar_t kSettingsFileName[] = L"notification-settings.ini";
    constexpr wchar_t kMetaSection[] = L"meta";
    constexpr wchar_t kPlacementSection[] = L"placement";

    void TraceSettingsMessage(const std::wstring& message)
    {
        OutputDebugStringW((message + L"\n").c_str());
    }

    std::wstring ReadIniString(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key)
    {
        std::wstring buffer(128, L'\0');
        for (;;)
        {
            const DWORD copied = GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
            if (copied < (buffer.size() - 1))
            {
                buffer.resize(copied);
                return buffer;
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    bool WriteIniValue(const std::filesystem::path& path, const wchar_t* section, const wchar_t* key, std::wstring_view value)
    {
        std::wstring persistedValue(value);
        if (WritePrivateProfileStringW(section, key, persistedValue.c_str(), path.c_str()) != FALSE)
        {
            return true;
        }

        std::wstring message = L"OneShot notification settings: failed to write [";
        message += section;
        message += L"] ";
        message += key;
        message += L" (error=";
        message += std::to_wstring(GetLastError());
        message += L")";
        TraceSettingsMessage(message);
        return false;
    }
}

namespace oneshot
{
    NotificationSettingsStore::NotificationSettingsStore(const AppPaths& paths)
        : _settingsPath(paths.localAppDataRoot / kSettingsFileName)
        , _placement(Load())
    {
    }

    const NotificationPlacement& NotificationSettingsStore::GetPlacement() const noexcept
    {
        return _placement;
    }

    void NotificationSettingsStore::SavePlacement(NotificationPlacement placement) noexcept
    {
        _placement = NormalizeNotificationPlacement(placement);
        PersistToDisk(_placement);
    }

    NotificationPlacement NotificationSettingsStore::Load() const noexcept
    {
        std::error_code existsError;
        const bool fileExists = std::filesystem::exists(_settingsPath, existsError);
        if (existsError || !fileExists)
        {
            return DefaultNotificationPlacement();
        }

        const auto anchor = ParseNotificationAnchor(ReadIniString(_settingsPath, kPlacementSection, L"anchor"));
        const auto growDirection = ParseNotificationGrowDirection(ReadIniString(_settingsPath, kPlacementSection, L"grow_direction"));
        if (!anchor.has_value() || !growDirection.has_value())
        {
            return DefaultNotificationPlacement();
        }

        if (!IsValidGrowDirection(*anchor, *growDirection))
        {
            return DefaultNotificationPlacement();
        }

        return NotificationPlacement{ *anchor, *growDirection };
    }

    void NotificationSettingsStore::PersistToDisk(NotificationPlacement placement) const noexcept
    {
        std::error_code directoryError;
        std::filesystem::create_directories(_settingsPath.parent_path(), directoryError);
        if (directoryError)
        {
            std::wstring message = L"OneShot notification settings: failed to create settings directory (error=";
            message += std::to_wstring(directoryError.value());
            message += L")";
            TraceSettingsMessage(message);
            return;
        }

        if (DeleteFileW(_settingsPath.c_str()) == FALSE)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND)
            {
                std::wstring message = L"OneShot notification settings: failed to replace existing settings file (error=";
                message += std::to_wstring(error);
                message += L")";
                TraceSettingsMessage(message);
            }
        }

        bool ok = true;
        ok = WriteIniValue(_settingsPath, kMetaSection, L"version", L"1") && ok;
        ok = WriteIniValue(_settingsPath, kPlacementSection, L"anchor", NotificationAnchorToString(placement.anchor)) && ok;
        ok = WriteIniValue(_settingsPath, kPlacementSection, L"grow_direction", NotificationGrowDirectionToString(placement.growDirection)) && ok;

        if (WritePrivateProfileStringW(nullptr, nullptr, nullptr, _settingsPath.c_str()) == FALSE)
        {
            std::wstring message = L"OneShot notification settings: failed to flush settings file (error=";
            message += std::to_wstring(GetLastError());
            message += L")";
            TraceSettingsMessage(message);
            return;
        }

        if (!ok)
        {
            TraceSettingsMessage(L"OneShot notification settings: placement updated in memory but disk persistence was incomplete.");
        }
    }
}
