using System.Diagnostics;
using System.Runtime.InteropServices;
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

    public SnapshotCoordinator(ScreenCaptureService captureService, Action<CapturedImage> onCaptureReady, Action<string>? log = null)
        : this(captureService, onCaptureReady, log, GetMonitorBounds, CreateSelectionSessionAsync)
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

    public async Task StartSnapshotAsync(string invocationId, Action<string, string, long, object?>? trace = null)
    {
        if (!await _snapshotGate.WaitAsync(0))
        {
            return;
        }

        var stopwatch = Stopwatch.StartNew();
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

    private static IReadOnlyList<System.Drawing.Rectangle> GetMonitorBounds()
    {
        nint previousDpiContext = NativeMethods.SetThreadDpiAwarenessContext(NativeMethods.DpiAwarenessContextPerMonitorAwareV2);
        try
        {
            var bounds = new List<System.Drawing.Rectangle>();
            bool success = NativeMethods.EnumDisplayMonitors(
                nint.Zero,
                nint.Zero,
                (hMonitor, _, _, _) =>
                {
                    if (NativeMethods.TryGetMonitorBounds(hMonitor, out var monitorBounds))
                    {
                        bounds.Add(monitorBounds);
                    }

                    return true;
                },
                nint.Zero);

            if (success && bounds.Count > 0)
            {
                return bounds;
            }

            // Fallback for environments where monitor enumeration fails unexpectedly.
            var fallbackLeft = NativeMethods.GetSystemMetrics(NativeMethods.SmXvirtualscreen);
            var fallbackTop = NativeMethods.GetSystemMetrics(NativeMethods.SmYvirtualscreen);
            var fallbackWidth = NativeMethods.GetSystemMetrics(NativeMethods.SmCxvirtualscreen);
            var fallbackHeight = NativeMethods.GetSystemMetrics(NativeMethods.SmCyvirtualscreen);
            return new[] { new System.Drawing.Rectangle(fallbackLeft, fallbackTop, fallbackWidth, fallbackHeight) };
        }
        finally
        {
            if (previousDpiContext != nint.Zero)
            {
                NativeMethods.SetThreadDpiAwarenessContext(previousDpiContext);
            }
        }
    }

    private static class NativeMethods
    {
        internal const int SmXvirtualscreen = 76;
        internal const int SmYvirtualscreen = 77;
        internal const int SmCxvirtualscreen = 78;
        internal const int SmCyvirtualscreen = 79;
        internal static readonly nint DpiAwarenessContextPerMonitorAwareV2 = new(-4);

        internal delegate bool MonitorEnumProc(nint hMonitor, nint hdcMonitor, nint lprcMonitor, nint dwData);

        [StructLayout(LayoutKind.Sequential)]
        internal struct RectStruct
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct MonitorInfoEx
        {
            public int cbSize;
            public RectStruct rcMonitor;
            public RectStruct rcWork;
            public uint dwFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string szDevice;
        }

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool EnumDisplayMonitors(nint hdc, nint lprcClip, MonitorEnumProc lpfnEnum, nint dwData);

        [DllImport("user32.dll")]
        internal static extern nint SetThreadDpiAwarenessContext(nint dpiContext);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetMonitorInfo(nint hMonitor, ref MonitorInfoEx lpmi);

        [DllImport("user32.dll")]
        internal static extern int GetSystemMetrics(int nIndex);

        internal static bool TryGetMonitorBounds(nint hMonitor, out System.Drawing.Rectangle bounds)
        {
            var monitorInfo = new MonitorInfoEx
            {
                cbSize = Marshal.SizeOf<MonitorInfoEx>(),
                szDevice = string.Empty
            };

            if (!GetMonitorInfo(hMonitor, ref monitorInfo))
            {
                bounds = default;
                return false;
            }

            bounds = new System.Drawing.Rectangle(
                monitorInfo.rcMonitor.Left,
                monitorInfo.rcMonitor.Top,
                monitorInfo.rcMonitor.Right - monitorInfo.rcMonitor.Left,
                monitorInfo.rcMonitor.Bottom - monitorInfo.rcMonitor.Top);
            return true;
        }
    }

    private sealed record SnapshotSession(
        System.Drawing.Rectangle VirtualScreenBounds,
        CapturedImage VirtualCapture,
        IReadOnlyList<MonitorSnapshot> MonitorSnapshots);
}
