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
    private readonly Stopwatch _startupStopwatch = Stopwatch.StartNew();
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
        Log($"App initialization started at {_startupStopwatch.ElapsedMilliseconds}ms.");
        _outputService = new OutputService();
        var captureService = new ScreenCaptureService();
        _notificationCoordinator = new NotificationCoordinator(_outputService);
        _snapshotCoordinator = new SnapshotCoordinator(captureService, _notificationCoordinator.ShowNotification, Log);

        _commandServer = new NamedPipeCommandServer(log: Log);
        _commandServer.CommandReceived += async (_, command) => await OnCommandAsync(command);
        _ = _commandServer.StartAsync();
        Log($"Named pipe server started at {_startupStopwatch.ElapsedMilliseconds}ms.");

        var startupService = new StartupService();
        startupService.EnsureRunAtLogin();

        InitializeTrayIcon();
        Log($"Tray initialization completed at {_startupStopwatch.ElapsedMilliseconds}ms.");

        if (args.Length > 0 && args[0].Equals("snapshot", StringComparison.OrdinalIgnoreCase))
        {
            Log($"Initial snapshot command dispatching at {_startupStopwatch.ElapsedMilliseconds}ms.");
            await Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync);
        }
    }

    private void OnExit()
    {
        _commandServer?.Dispose();

        _notificationCoordinator?.CloseAll();

        _trayIcon?.Dispose();
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

    private async Task OnCommandAsync(AppCommand command)
    {
        if (command == AppCommand.Snapshot)
        {
            Log($"Snapshot command received at {_startupStopwatch.ElapsedMilliseconds}ms; dispatching to UI thread.");
            await Dispatcher.UIThread.InvokeAsync(StartSnapshotFromUiAsync);
        }
    }

    private async Task StartSnapshotFromUiAsync()
    {
        if (_snapshotCoordinator is null)
        {
            return;
        }

        Log($"StartSnapshotFromUiAsync entered at {_startupStopwatch.ElapsedMilliseconds}ms.");
        await _snapshotCoordinator.StartSnapshotAsync();
    }

    private static void Log(string message)
    {
        Debug.WriteLine(message);
    }
}
