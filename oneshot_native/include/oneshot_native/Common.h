#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace oneshot
{
    inline constexpr wchar_t kAppName[] = L"OneShot Native";
    inline constexpr wchar_t kWindowClassName[] = L"OneShotNative.AppHost";
    inline constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\OneShot.Command.v1";
    inline constexpr wchar_t kMutexName[] = L"Local\\OneShot.Native.Instance.v1";

    inline constexpr UINT kTrayIconId = 1001;
    inline constexpr UINT kWindowMessageTrayIcon = WM_APP + 1;

    inline constexpr UINT_PTR kTrayMenuSnapshot = 40001;
    inline constexpr UINT_PTR kTrayMenuDiagnostics = 40002;
    inline constexpr UINT_PTR kTrayMenuExit = 40003;

    inline constexpr wchar_t kCommandSnapshot[] = L"snapshot";
    inline constexpr wchar_t kCommandInstallStartup[] = L"install-startup";
    inline constexpr wchar_t kCommandUninstallStartup[] = L"uninstall-startup";
    inline constexpr wchar_t kCommandDiagnostics[] = L"diagnostics";
    inline constexpr wchar_t kCommandPing[] = L"ping";
    inline constexpr wchar_t kCommandExit[] = L"exit";
}
