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
    private readonly SnapshotTraceRecorder _traceRecorder = new("daemon");
    private readonly Dictionary<string, Stopwatch> _invocationStopwatches = new();
    private NotifyIcon? _trayIcon;
    private NamedPipeCommandServer? _commandServer;
    private OutputService? _outputService;
    private SnapshotCoordinator? _snapshotCoordinator;
    private NotificationCoordinator? _notificationCoordinator;
    private SelectionOverlayPool? _overlayPool;
    private IClassicDesktopStyleApplicationLifetime? _desktop;
    private AppCommandEnvelope? _initialEnvelope;
    private const string OverlayPoolTraceInvocationId = "overlay-pool-startup";

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
        _initialEnvelope = AppLaunchBootstrap.ConsumeInitialEnvelope();
        if (_initialEnvelope is null && AppLaunchBootstrap.ParseCommand(args) == AppCommand.Snapshot)
        {
            _initialEnvelope = AppCommandEnvelope.Create(AppCommand.Snapshot);
        }
        _outputService = new OutputService();
        var captureService = new ScreenCaptureService();
        _notificationCoordinator = new NotificationCoordinator(_outputService);
        _overlayPool = new SelectionOverlayPool(MonitorTopologyService.GetMonitorBounds);
        _snapshotCoordinator = new SnapshotCoordinator(captureService, image =>
        {
            _notificationCoordinator.ShowNotification(image);
            var invocationId = _activeInvocationId;
            if (!string.IsNullOrWhiteSpace(invocationId))
            {
                RecordDaemonTrace(invocationId, "capture_ready_callback_complete");
            }
        }, Log, MonitorTopologyService.GetMonitorBounds, CreateSelectionSessionAsync);

        _commandServer = new NamedPipeCommandServer(log: Log);
        _commandServer.CommandReceived += async (_, command) => await OnCommandAsync(command);
        _ = _commandServer.StartAsync();
        Log($"Named pipe server started at {_startupStopwatch.ElapsedMilliseconds}ms.");

        var startupService = new StartupService();
        startupService.EnsureRunAtLogin();

        InitializeTrayIcon();
        Log($"Tray initialization completed at {_startupStopwatch.ElapsedMilliseconds}ms.");
        _ = Dispatcher.UIThread.InvokeAsync(
            async () =>
            {
                if (_overlayPool is null)
                {
                    return;
                }

                await _overlayPool.PrewarmAsync(OverlayPoolTraceInvocationId, RecordSnapshotPhase);
            },
            DispatcherPriority.Background);

        if (args.Length > 0 && args[0].Equals("snapshot", StringComparison.OrdinalIgnoreCase))
        {
            Log($"Initial snapshot command dispatching at {_startupStopwatch.ElapsedMilliseconds}ms.");
            var envelope = _initialEnvelope ?? AppCommandEnvelope.Create(AppCommand.Snapshot);
            StartInvocationTrace(envelope);
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_start", new { Source = "startup" });
            await Dispatcher.UIThread.InvokeAsync(() => StartSnapshotFromUiAsync(envelope));
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_complete", new { Source = "startup" });
        }
    }

    private void OnExit()
    {
        _commandServer?.Dispose();
        _overlayPool?.Dispose();

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
        menu.Items.Add("Take Snapshot", null, (_, _) =>
        {
            var envelope = AppCommandEnvelope.Create(AppCommand.Snapshot);
            StartInvocationTrace(envelope);
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_start", new { Source = "tray_menu" });
            _ = Dispatcher.UIThread.InvokeAsync(async () =>
            {
                await StartSnapshotFromUiAsync(envelope);
                RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_complete", new { Source = "tray_menu" });
            });
        });
        menu.Items.Add("Exit", null, (_, _) => Dispatcher.UIThread.Post(() => _desktop?.Shutdown()));
        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.DoubleClick += (_, _) =>
        {
            var envelope = AppCommandEnvelope.Create(AppCommand.Snapshot);
            StartInvocationTrace(envelope);
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_start", new { Source = "tray_double_click" });
            _ = Dispatcher.UIThread.InvokeAsync(async () =>
            {
                await StartSnapshotFromUiAsync(envelope);
                RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_complete", new { Source = "tray_double_click" });
            });
        };
    }

    private string? _activeInvocationId;

    private async Task OnCommandAsync(AppCommandEnvelope envelope)
    {
        if (envelope.Command == AppCommand.Snapshot)
        {
            Log($"Snapshot command received at {_startupStopwatch.ElapsedMilliseconds}ms; dispatching to UI thread.");
            StartInvocationTrace(envelope);
            RecordDaemonTrace(envelope.InvocationId, "daemon_command_received");
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_start", new { Source = "pipe" });
            await Dispatcher.UIThread.InvokeAsync(() => StartSnapshotFromUiAsync(envelope));
            RecordDaemonTrace(envelope.InvocationId, "daemon_ui_dispatch_complete", new { Source = "pipe" });
        }
    }

    private async Task StartSnapshotFromUiAsync(AppCommandEnvelope envelope)
    {
        if (_snapshotCoordinator is null)
        {
            return;
        }

        _activeInvocationId = envelope.InvocationId;
        Log($"StartSnapshotFromUiAsync entered at {_startupStopwatch.ElapsedMilliseconds}ms.");
        RecordDaemonTrace(envelope.InvocationId, "start_snapshot_entered");
        try
        {
            await _snapshotCoordinator.StartSnapshotAsync(envelope.InvocationId, RecordSnapshotPhase);
        }
        finally
        {
            _invocationStopwatches.Remove(envelope.InvocationId);
            if (_activeInvocationId == envelope.InvocationId)
            {
                _activeInvocationId = null;
            }
        }
    }

    private void RecordSnapshotPhase(string invocationId, string phase, long elapsedMs, object? details)
    {
        _traceRecorder.Record(invocationId, phase, elapsedMs, details);
    }

    private void StartInvocationTrace(AppCommandEnvelope envelope)
    {
        _invocationStopwatches[envelope.InvocationId] = Stopwatch.StartNew();
    }

    private void RecordDaemonTrace(string invocationId, string phase, object? details = null)
    {
        if (_invocationStopwatches.TryGetValue(invocationId, out var stopwatch))
        {
            _traceRecorder.Record(invocationId, phase, stopwatch.ElapsedMilliseconds, details);
        }
    }

    private Task<Rect?> CreateSelectionSessionAsync(string invocationId, IReadOnlyList<MonitorSnapshot> monitorSnapshots, Action<string, string, long, object?>? trace)
    {
        Func<MonitorSnapshot, ValueTask<PooledOverlayLease>> overlayLeaseFactory;
        if (_overlayPool is null)
        {
            overlayLeaseFactory = snapshot =>
            {
                var surface = new Windows.SelectionOverlayWindow();
                surface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
                return ValueTask.FromResult(new PooledOverlayLease(surface, releasedSurface => releasedSurface.Close()));
            };
        }
        else
        {
            overlayLeaseFactory = snapshot => _overlayPool.AcquireAsync(snapshot, invocationId, trace);
        }

        var overlaySession = new MultiMonitorSelectionSession(
            monitorSnapshots,
            overlayLeaseFactory: overlayLeaseFactory,
            invocationId: invocationId,
            trace: trace);
        return overlaySession.GetSelectionAsync();
    }

    private static void Log(string message)
    {
        Debug.WriteLine(message);
    }
}
