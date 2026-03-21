using System.Diagnostics;
using Avalonia;
using OneShot.Models;

namespace OneShot.Services;

public sealed class SnapshotCoordinator
{
    private readonly ScreenCaptureService _captureService;
    private readonly SemaphoreSlim _snapshotGate = new(1, 1);
    private readonly Action<CapturedImage> _onCaptureReady;
    private readonly Func<IReadOnlyList<System.Drawing.Rectangle>> _monitorBoundsProvider;
    private readonly Func<string, IReadOnlyList<MonitorSnapshot>, Action<string, string, long, object?>?, Task<Rect?>> _selectionProvider;
    private readonly Action<string>? _log;
    private int _activeSnapshotCount;

    public SnapshotCoordinator(ScreenCaptureService captureService, Action<CapturedImage> onCaptureReady, Action<string>? log = null)
        : this(captureService, onCaptureReady, log, MonitorTopologyService.GetMonitorBounds, CreateSelectionSessionAsync)
    {
    }

    internal SnapshotCoordinator(
        ScreenCaptureService captureService,
        Action<CapturedImage> onCaptureReady,
        Action<string>? log,
        Func<IReadOnlyList<System.Drawing.Rectangle>> monitorBoundsProvider,
        Func<string, IReadOnlyList<MonitorSnapshot>, Action<string, string, long, object?>?, Task<Rect?>> selectionProvider)
    {
        _captureService = captureService;
        _onCaptureReady = onCaptureReady;
        _log = log;
        _monitorBoundsProvider = monitorBoundsProvider;
        _selectionProvider = selectionProvider;
    }

    public bool IsSnapshotActive => Volatile.Read(ref _activeSnapshotCount) > 0;

    public async Task StartSnapshotAsync(string invocationId, Action<string, string, long, object?>? trace = null)
    {
        if (!await _snapshotGate.WaitAsync(0))
        {
            trace?.Invoke(invocationId, "snapshot_gate_busy", 0, null);
            return;
        }

        var stopwatch = Stopwatch.StartNew();
        Interlocked.Increment(ref _activeSnapshotCount);
        try
        {
            Log("StartSnapshotAsync entered.");
            var snapshotSession = CaptureSnapshotSession(invocationId, trace);
            Log($"Monitor snapshot preparation completed in {stopwatch.ElapsedMilliseconds}ms.");
            trace?.Invoke(invocationId, "selection_session_start", stopwatch.ElapsedMilliseconds, new { MonitorCount = snapshotSession.MonitorSnapshots.Count });
            Log($"Overlay show starting at {stopwatch.ElapsedMilliseconds}ms.");
            var rect = await _selectionProvider(invocationId, snapshotSession.MonitorSnapshots, trace);
            if (rect is null)
            {
                return;
            }

            var selectedRect = rect.Value;
            trace?.Invoke(invocationId, "selection_complete", stopwatch.ElapsedMilliseconds, new { selectedRect.X, selectedRect.Y, selectedRect.Width, selectedRect.Height });
            Log($"Selection completed at {stopwatch.ElapsedMilliseconds}ms.");
            Log($"Snapshot selection: x={selectedRect.X}, y={selectedRect.Y}, w={selectedRect.Width}, h={selectedRect.Height}");
            var selectionInVirtualCapture = SnapshotSelectionMapper.ToVirtualCaptureRect(selectedRect, snapshotSession.VirtualScreenBounds);
            var capture = _captureService.Crop(snapshotSession.VirtualCapture, selectionInVirtualCapture);
            trace?.Invoke(invocationId, "final_crop_complete", stopwatch.ElapsedMilliseconds, new { capture.PixelWidth, capture.PixelHeight });
            Log($"Final crop completed in {stopwatch.ElapsedMilliseconds}ms.");
            _onCaptureReady(capture);
        }
        catch (Exception ex)
        {
            Log($"Snapshot flow failed: {ex}");
        }
        finally
        {
            Interlocked.Decrement(ref _activeSnapshotCount);
            _snapshotGate.Release();
        }
    }

    private void Log(string message)
    {
        if (_log is not null)
        {
            _log(message);
            return;
        }

        Debug.WriteLine(message);
    }

    private SnapshotSession CaptureSnapshotSession(string invocationId, Action<string, string, long, object?>? trace)
    {
        var stopwatch = Stopwatch.StartNew();
        var monitorBounds = _monitorBoundsProvider();
        trace?.Invoke(invocationId, "monitor_enumeration_complete", stopwatch.ElapsedMilliseconds, new { MonitorCount = monitorBounds.Count });
        Log($"Monitor enumeration completed in {stopwatch.ElapsedMilliseconds}ms; monitorCount={monitorBounds.Count}.");
        var virtualScreenBounds = GetVirtualScreenBounds(monitorBounds);
        var virtualScreenRect = new Rect(
            virtualScreenBounds.Left,
            virtualScreenBounds.Top,
            virtualScreenBounds.Width,
            virtualScreenBounds.Height);
        trace?.Invoke(invocationId, "virtual_capture_start", stopwatch.ElapsedMilliseconds, new { virtualScreenBounds.Left, virtualScreenBounds.Top, virtualScreenBounds.Width, virtualScreenBounds.Height });
        var virtualCapture = _captureService.Capture(virtualScreenRect);
        trace?.Invoke(invocationId, "virtual_capture_complete", stopwatch.ElapsedMilliseconds, new { virtualCapture.PixelWidth, virtualCapture.PixelHeight });
        Log($"Raw desktop capture completed in {stopwatch.ElapsedMilliseconds}ms; bounds={virtualScreenBounds}.");
        trace?.Invoke(invocationId, "monitor_snapshot_prepare_start", stopwatch.ElapsedMilliseconds, new { MonitorCount = monitorBounds.Count });
        var monitorSnapshots = new List<MonitorSnapshot>();

        foreach (var bounds in monitorBounds)
        {
            var selectionInVirtualCapture = SnapshotSelectionMapper.ToVirtualCaptureRect(
                new Rect(bounds.Left, bounds.Top, bounds.Width, bounds.Height),
                virtualScreenBounds);
            var monitorCapture = _captureService.Crop(virtualCapture, selectionInVirtualCapture);
            monitorSnapshots.Add(new MonitorSnapshot(bounds, monitorCapture));
        }

        trace?.Invoke(invocationId, "monitor_snapshot_prepare_complete", stopwatch.ElapsedMilliseconds, new { MonitorCount = monitorSnapshots.Count });
        Log($"Monitor snapshot preparation stage finished in {stopwatch.ElapsedMilliseconds}ms.");
        return new SnapshotSession(virtualScreenBounds, virtualCapture, monitorSnapshots);
    }

    private static System.Drawing.Rectangle GetVirtualScreenBounds(IReadOnlyList<System.Drawing.Rectangle> monitorBounds)
    {
        if (monitorBounds.Count == 0)
        {
            return new System.Drawing.Rectangle(0, 0, 1, 1);
        }

        return monitorBounds.Aggregate(System.Drawing.Rectangle.Union);
    }

    private static Task<Rect?> CreateSelectionSessionAsync(string invocationId, IReadOnlyList<MonitorSnapshot> monitorSnapshots, Action<string, string, long, object?>? trace)
    {
        var overlaySession = new MultiMonitorSelectionSession(monitorSnapshots, invocationId: invocationId, trace: trace);
        return overlaySession.GetSelectionAsync();
    }

    private sealed record SnapshotSession(
        System.Drawing.Rectangle VirtualScreenBounds,
        CapturedImage VirtualCapture,
        IReadOnlyList<MonitorSnapshot> MonitorSnapshots);
}
