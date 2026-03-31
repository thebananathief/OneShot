#include "oneshot_native/AppHost.h"

#include <iostream>

namespace
{
    constexpr UINT_PTR kOverlayPrewarmTimerId = 1;

    void WriteConsoleText(const std::wstring& text)
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
        {
            return;
        }

        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE)
        {
            FreeConsole();
            return;
        }

        DWORD written = 0;
        WriteConsoleW(output, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        FreeConsole();
    }
}

namespace oneshot
{
    StartupMode ParseStartupMode()
    {
        const auto commandLine = GetCommandLineW();
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(commandLine, &argc);
        if (!argv || argc <= 1)
        {
            if (argv)
            {
                LocalFree(argv);
            }
            return StartupMode::Daemon;
        }

        const std::wstring arg = argv[1];
        LocalFree(argv);

        if (arg == kCommandSnapshot)
        {
            return StartupMode::SendSnapshot;
        }
        if (arg == kCommandInstallStartup)
        {
            return StartupMode::InstallStartup;
        }
        if (arg == kCommandUninstallStartup)
        {
            return StartupMode::UninstallStartup;
        }
        if (arg == kCommandDiagnostics)
        {
            return StartupMode::Diagnostics;
        }
        if (arg == kCommandExit)
        {
            return StartupMode::ExitDaemon;
        }

        return StartupMode::Daemon;
    }

    AppHost::AppHost(StartupMode mode)
        : _mode(mode)
        , _paths(ResolveAppPaths())
        , _tempFileManager(_paths)
        , _startupService(_paths)
        , _diagnostics(_paths)
        , _outputService(_paths)
        , _notificationManager(_paths, _outputService)
        , _server([this](const CommandEnvelope& envelope) { return HandleCommand(envelope); })
    {
    }

    AppHost::~AppHost()
    {
        _notificationManager.CloseAll();
        _server.Stop();
        _tray.Dispose();

        if (_hwnd)
        {
            DestroyWindow(_hwnd);
        }

        if (_instanceMutex)
        {
            CloseHandle(_instanceMutex);
        }
    }

    int AppHost::Run()
    {
        _tempFileManager.Initialize();

        if (_mode == StartupMode::Diagnostics)
        {
            ShowDiagnosticsAndExit();
            return 0;
        }

        if (_mode == StartupMode::InstallStartup)
        {
            InstallStartupAndExit();
            return 0;
        }

        if (_mode == StartupMode::UninstallStartup)
        {
            UninstallStartupAndExit();
            return 0;
        }

        if (!EnsurePrimaryOrForward())
        {
            return 0;
        }

        if (!CreateMainWindow())
        {
            return 1;
        }

        _tray.Initialize(_hwnd);
        _server.Start();
        SetTimer(_hwnd, kOverlayPrewarmTimerId, 750, nullptr);

        if (_mode == StartupMode::SendSnapshot)
        {
            PostMessageW(_hwnd, WM_COMMAND, kTrayMenuSnapshot, 0);
        }
        else if (_mode == StartupMode::ExitDaemon)
        {
            PostMessageW(_hwnd, WM_CLOSE, 0, 0);
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }

    bool AppHost::CreateMainWindow()
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &AppHost::WindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kWindowClassName;

        RegisterClassW(&windowClass);

        _hwnd = CreateWindowExW(
            0,
            kWindowClassName,
            kAppName,
            0,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            this);

        return _hwnd != nullptr;
    }

    bool AppHost::EnsurePrimaryOrForward()
    {
        _instanceMutex = CreateMutexW(nullptr, FALSE, kMutexName);
        const auto alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
        _runningAsPrimary = !alreadyRunning;

        if (_runningAsPrimary)
        {
            return true;
        }

        CommandEnvelope envelope{};
        envelope.command = _mode == StartupMode::ExitDaemon ? kCommandExit : kCommandSnapshot;
        envelope.requestId = NewRequestId();
        envelope.sentAt = CurrentIso8601Utc();

        std::wstring response;
        _client.Send(envelope, 1500, response);
        return false;
    }

    void AppHost::HandleSnapshotRequested()
    {
        bool expected = false;
        if (!_snapshotActive.compare_exchange_strong(expected, true))
        {
            return;
        }

        const auto resetSnapshotActive = [this]()
        {
            _snapshotActive.store(false);
        };

        if (_overlayManager.IsSelectionActive())
        {
            resetSnapshotActive();
            return;
        }

        const auto virtualCapture = _captureService.CaptureVirtualScreen();
        if (!virtualCapture.has_value())
        {
            _tray.ShowBalloon(L"OneShot", L"Virtual screen capture failed.");
            resetSnapshotActive();
            return;
        }

        const auto selection = _overlayManager.SelectRegion(*virtualCapture, _hwnd);
        if (!selection.has_value())
        {
            resetSnapshotActive();
            return;
        }

        auto capture = _captureService.Crop(*virtualCapture, *selection);
        if (!capture.has_value())
        {
            _tray.ShowBalloon(L"OneShot", L"Failed to crop the selected region.");
            resetSnapshotActive();
            return;
        }

        std::wstring outputError;
        const auto savedPath = _outputService.BuildSavedScreenshotPath(capture->capturedAtUtc);
        if (!_outputService.SavePng(*capture, savedPath, outputError))
        {
            const auto message = std::wstring(L"Save failed: ") + outputError;
            _tray.ShowBalloon(L"OneShot", message);
            resetSnapshotActive();
            return;
        }

        if (!_outputService.CopyToClipboard(_hwnd, *capture, outputError))
        {
            const auto message = std::wstring(L"Clipboard copy failed: ") + outputError;
            _tray.ShowBalloon(L"OneShot", message);
        }

        _notificationManager.Show(_hwnd, std::move(*capture), savedPath, savedPath);
        if (_notificationManager.GetDebugState().showAttempted && !_notificationManager.GetDebugState().windowVisible)
        {
            _tray.ShowBalloon(L"OneShot", L"Screenshot saved, but notification failed to display.");
        }
        resetSnapshotActive();
    }

    CommandResponse AppHost::HandleCommand(const CommandEnvelope& envelope)
    {
        CommandResponse response{};
        response.ok = true;
        response.requestId = envelope.requestId;

        if (envelope.command == kCommandSnapshot)
        {
            if (_snapshotActive.load())
            {
                response.message = L"snapshot busy";
            }
            else
            {
                PostMessageW(_hwnd, WM_COMMAND, kTrayMenuSnapshot, 0);
                response.message = L"snapshot queued";
            }
        }
        else if (envelope.command == kCommandInstallStartup)
        {
            response.ok = _startupService.Install(response.message);
        }
        else if (envelope.command == kCommandUninstallStartup)
        {
            response.ok = _startupService.Uninstall(response.message);
        }
        else if (envelope.command == kCommandDiagnostics)
        {
            response.message = _diagnostics.BuildDiagnosticsText(_startupService.IsEnabled(), _snapshotActive.load(), _notificationManager.GetDebugState());
        }
        else if (envelope.command == kCommandPing)
        {
            response.message = L"pong";
        }
        else if (envelope.command == kCommandExit)
        {
            PostMessageW(_hwnd, WM_CLOSE, 0, 0);
            response.message = L"shutdown requested";
        }
        else
        {
            response.ok = false;
            response.message = L"unsupported command";
        }

        return response;
    }

    void AppHost::ShowDiagnosticsAndExit() const
    {
        WriteConsoleText(_diagnostics.BuildDiagnosticsText(_startupService.IsEnabled(), _snapshotActive.load(), _notificationManager.GetDebugState()));
    }

    void AppHost::InstallStartupAndExit() const
    {
        std::wstring message;
        const auto ok = _startupService.Install(message);
        WriteConsoleText(std::wstring(ok ? L":ok " : L":error ") + message + L"\n");
    }

    void AppHost::UninstallStartupAndExit() const
    {
        std::wstring message;
        const auto ok = _startupService.Uninstall(message);
        WriteConsoleText(std::wstring(ok ? L":ok " : L":error ") + message + L"\n");
    }

    LRESULT CALLBACK AppHost::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        AppHost* self = reinterpret_cast<AppHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto createStruct = reinterpret_cast<LPCREATESTRUCTW>(lParam);
            self = static_cast<AppHost*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        if (!self)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        default:
            if (message == self->_tray.ExplorerRestartMessage())
            {
                self->_tray.Restore(hwnd);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kOverlayPrewarmTimerId)
            {
                KillTimer(hwnd, kOverlayPrewarmTimerId);
                self->_overlayManager.Prewarm(hwnd);
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case kTrayMenuSnapshot:
                self->HandleSnapshotRequested();
                return 0;
            case kTrayMenuDiagnostics:
                MessageBoxW(hwnd, self->_diagnostics.BuildDiagnosticsText(self->_startupService.IsEnabled(), self->_snapshotActive.load(), self->_notificationManager.GetDebugState()).c_str(), kAppName, MB_OK | MB_ICONINFORMATION);
                return 0;
            case kTrayMenuExit:
                DestroyWindow(hwnd);
                return 0;
            default:
                break;
            }
            break;
        case kWindowMessageTrayIcon:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
            {
                self->HandleSnapshotRequested();
                return 0;
            }
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
            {
                self->_tray.ShowContextMenu(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
