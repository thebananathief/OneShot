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
    private readonly Action<string>? _log;

    public SnapshotCoordinator(ScreenCaptureService captureService, Action<CapturedImage> onCaptureReady, Action<string>? log = null)
    {
        _captureService = captureService;
        _onCaptureReady = onCaptureReady;
        _log = log;
    }

    public async Task StartSnapshotAsync()
    {
        if (!await _snapshotGate.WaitAsync(0))
        {
            return;
        }

        try
        {
            var virtualScreen = GetVirtualScreenBounds();
            var monitorSnapshots = CaptureMonitorSnapshots(virtualScreen);
            var overlaySession = new MultiMonitorSelectionSession(monitorSnapshots, log: Log);
            var rect = await overlaySession.GetSelectionAsync();
            if (rect is null)
            {
                return;
            }

            var selectedRect = rect.Value;
            Log($"Snapshot selection: x={selectedRect.X}, y={selectedRect.Y}, w={selectedRect.Width}, h={selectedRect.Height}");
            var capture = _captureService.Capture(selectedRect);
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

    private static System.Drawing.Rectangle GetVirtualScreenBounds()
    {
        var monitorBounds = GetMonitorBounds();
        if (monitorBounds.Count == 0)
        {
            return new System.Drawing.Rectangle(0, 0, 1, 1);
        }

        return monitorBounds
            .Aggregate(System.Drawing.Rectangle.Union);
    }

    private IReadOnlyList<MonitorSnapshot> CaptureMonitorSnapshots(System.Drawing.Rectangle virtualScreenBounds)
    {
        var virtualScreenRect = new Rect(virtualScreenBounds.Left, virtualScreenBounds.Top, virtualScreenBounds.Width, virtualScreenBounds.Height);
        var virtualCapture = _captureService.Capture(virtualScreenRect);
        var monitorSnapshots = new List<MonitorSnapshot>();
        var monitorBounds = GetMonitorBounds();

        foreach (var bounds in monitorBounds)
        {
            var selectionInVirtualCapture = new Rect(
                bounds.Left - virtualScreenBounds.Left,
                bounds.Top - virtualScreenBounds.Top,
                bounds.Width,
                bounds.Height);
            var monitorCapture = _captureService.Crop(virtualCapture, selectionInVirtualCapture);
            monitorSnapshots.Add(new MonitorSnapshot(bounds, monitorCapture));
        }

        return monitorSnapshots;
    }

    private static IReadOnlyList<System.Drawing.Rectangle> GetMonitorBounds()
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

    private static class NativeMethods
    {
        internal const int SmXvirtualscreen = 76;
        internal const int SmYvirtualscreen = 77;
        internal const int SmCxvirtualscreen = 78;
        internal const int SmCyvirtualscreen = 79;

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
}
