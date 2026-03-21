#pragma once

#include "oneshot_native/CaptureService.h"
#include "oneshot_native/CommandClient.h"
#include "oneshot_native/CommandServer.h"
#include "oneshot_native/DiagnosticsService.h"
#include "oneshot_native/OutputService.h"
#include "oneshot_native/StartupService.h"
#include "oneshot_native/TempFileManager.h"
#include "oneshot_native/TrayController.h"

namespace oneshot
{
    enum class StartupMode
    {
        Daemon,
        SendSnapshot,
        InstallStartup,
        UninstallStartup,
        Diagnostics,
        ExitDaemon
    };

    StartupMode ParseStartupMode();

    class AppHost
    {
    public:
        explicit AppHost(StartupMode mode);
        ~AppHost();

        int Run();

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool CreateMainWindow();
        bool EnsurePrimaryOrForward();
        void HandleSnapshotRequested();
        CommandResponse HandleCommand(const CommandEnvelope& envelope);
        void ShowDiagnosticsAndExit() const;
        void InstallStartupAndExit() const;
        void UninstallStartupAndExit() const;

        StartupMode _mode;
        AppPaths _paths;
        TempFileManager _tempFileManager;
        StartupService _startupService;
        DiagnosticsService _diagnostics;
        CaptureService _captureService;
        OutputService _outputService;
        TrayController _tray;
        CommandServer _server;
        CommandClient _client;
        HANDLE _instanceMutex{nullptr};
        HWND _hwnd{nullptr};
        bool _runningAsPrimary{false};
    };
}
