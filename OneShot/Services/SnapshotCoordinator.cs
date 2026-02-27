using System.Diagnostics;
using Avalonia;
using OneShot.Models;
using OneShot.Windows;

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
            var virtualScreen = System.Windows.Forms.SystemInformation.VirtualScreen;
            var virtualRect = new Rect(virtualScreen.Left, virtualScreen.Top, virtualScreen.Width, virtualScreen.Height);
            var fullSpanCapture = _captureService.Capture(virtualRect);

            var overlay = new SelectionOverlayWindow(fullSpanCapture, virtualScreen);
            var rect = await overlay.GetSelectionAsync();
            if (rect is null)
            {
                return;
            }

            var sourceRect = new Rect(
                rect.Value.X - virtualScreen.Left,
                rect.Value.Y - virtualScreen.Top,
                rect.Value.Width,
                rect.Value.Height);

            var capture = _captureService.Crop(fullSpanCapture, sourceRect);
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
}
