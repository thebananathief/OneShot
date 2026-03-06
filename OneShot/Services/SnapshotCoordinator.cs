using System.Diagnostics;
using Avalonia;
using OneShot.Models;
using Screen = System.Windows.Forms.Screen;

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
        var screens = Screen.AllScreens;
        if (screens.Length == 0)
        {
            return Screen.PrimaryScreen?.Bounds ?? new System.Drawing.Rectangle(0, 0, 1, 1);
        }

        return screens
            .Select(screen => screen.Bounds)
            .Aggregate(System.Drawing.Rectangle.Union);
    }

    private IReadOnlyList<MonitorSnapshot> CaptureMonitorSnapshots(System.Drawing.Rectangle virtualScreenBounds)
    {
        var virtualScreenRect = new Rect(virtualScreenBounds.Left, virtualScreenBounds.Top, virtualScreenBounds.Width, virtualScreenBounds.Height);
        var virtualCapture = _captureService.Capture(virtualScreenRect);
        var monitorSnapshots = new List<MonitorSnapshot>();

        foreach (var screen in Screen.AllScreens)
        {
            System.Drawing.Rectangle monitorBounds = screen.Bounds;
            var selectionInVirtualCapture = new Rect(
                monitorBounds.Left - virtualScreenBounds.Left,
                monitorBounds.Top - virtualScreenBounds.Top,
                monitorBounds.Width,
                monitorBounds.Height);
            var monitorCapture = _captureService.Crop(virtualCapture, selectionInVirtualCapture);
            monitorSnapshots.Add(new MonitorSnapshot(monitorBounds, monitorCapture));
        }

        return monitorSnapshots;
    }
}
