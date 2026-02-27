using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using NotifyIcon = System.Windows.Forms.NotifyIcon;
using ContextMenuStrip = System.Windows.Forms.ContextMenuStrip;
using OneShot.Models;
using OneShot.Services;

namespace OneShot;

public partial class App : Application
{
    private const string MutexName = "OneShot.Singleton.v1";

    private Mutex? _instanceMutex;
    private NotifyIcon? _trayIcon;
    private NamedPipeCommandServer? _commandServer;
    private OutputService? _outputService;
    private SnapshotCoordinator? _snapshotCoordinator;
    private NotificationCoordinator? _notificationCoordinator;
    private IClassicDesktopStyleApplicationLifetime? _desktop;

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            _desktop = desktop;
            desktop.ShutdownMode = Avalonia.Controls.ShutdownMode.OnExplicitShutdown;
            desktop.Exit += (_, _) => OnExit();
            _ = InitializeAsync(desktop.Args ?? Array.Empty<string>());
        }

        base.OnFrameworkInitializationCompleted();
    }

    private async Task InitializeAsync(string[] args)
    {
        _instanceMutex = new Mutex(true, MutexName, out bool isPrimary);
        if (!isPrimary)
        {
            await ForwardCommandToPrimaryAsync(args);
            _desktop?.Shutdown();
            return;
        }

        _outputService = new OutputService();
        var captureService = new ScreenCaptureService();
        _notificationCoordinator = new NotificationCoordinator(_outputService);
        _snapshotCoordinator = new SnapshotCoordinator(captureService, _notificationCoordinator.ShowNotification, Log);
        var startupService = new StartupService();
        startupService.EnsureRunAtLogin();

        InitializeTrayIcon();

        _commandServer = new NamedPipeCommandServer(log: Log);
        _commandServer.CommandReceived += async (_, command) => await OnCommandAsync(command);
        _ = _commandServer.StartAsync();

        if (args.Length > 0 && args[0].Equals("snapshot", StringComparison.OrdinalIgnoreCase))
        {
            await Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync);
        }
    }

    private void OnExit()
    {
        _commandServer?.Dispose();

        _notificationCoordinator?.CloseAll();

        _trayIcon?.Dispose();
        if (_instanceMutex is not null)
        {
            try
            {
                _instanceMutex.ReleaseMutex();
            }
            catch
            {
            }

            _instanceMutex.Dispose();
        }
    }

    private void InitializeTrayIcon()
    {
        _trayIcon = new NotifyIcon
        {
            Text = "OneShot",
            Icon = System.Drawing.SystemIcons.Application,
            Visible = true
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Take Snapshot", null, (_, _) => _ = Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync));
        menu.Items.Add("Exit", null, (_, _) => Dispatcher.UIThread.Post(() => _desktop?.Shutdown()));
        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.DoubleClick += (_, _) => _ = Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync);
    }

    private async Task ForwardCommandToPrimaryAsync(string[] args)
    {
        if (args.Length == 0)
        {
            return;
        }

        var command = args[0].ToLowerInvariant() switch
        {
            "snapshot" => AppCommand.Snapshot,
            _ => AppCommand.None
        };

        if (command == AppCommand.None)
        {
            return;
        }

        using var client = new NamedPipeCommandClient(log: Log);
        await client.SendAsync(command);
    }

    private async Task OnCommandAsync(AppCommand command)
    {
        if (command == AppCommand.Snapshot)
        {
            await Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync);
        }
    }

    private async Task StartSnapshotFromUiAsync()
    {
        if (_snapshotCoordinator is null)
        {
            return;
        }

        await _snapshotCoordinator.StartSnapshotAsync();
    }

    private static void Log(string message)
    {
        Debug.WriteLine(message);
    }
}
