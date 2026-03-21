#include "oneshot_native/StartupService.h"

namespace
{
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kValueName[] = L"OneShot";
}

namespace oneshot
{
    StartupService::StartupService(AppPaths paths)
        : _paths(std::move(paths))
    {
    }

    bool StartupService::Install(std::wstring& message) const
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        {
            message = L"failed to open run key";
            return false;
        }

        const auto command = L"\"" + ToWideString(_paths.executablePath) + L"\"";
        const auto result = RegSetValueExW(
            key,
            kValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));

        RegCloseKey(key);
        message = result == ERROR_SUCCESS ? L"startup installed" : L"failed to write startup entry";
        return result == ERROR_SUCCESS;
    }

    bool StartupService::Uninstall(std::wstring& message) const
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            message = L"startup not configured";
            return false;
        }

        const auto result = RegDeleteValueW(key, kValueName);
        RegCloseKey(key);

        message = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) ? L"startup removed" : L"failed to remove startup entry";
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    bool StartupService::IsEnabled() const
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD type = 0;
        const auto result = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, nullptr);
        RegCloseKey(key);
        return result == ERROR_SUCCESS && type == REG_SZ;
    }
}
