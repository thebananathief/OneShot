using System.Diagnostics;
using System.Runtime.InteropServices;
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
            var virtualScreen = GetVirtualScreenBounds();
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

    private static System.Drawing.Rectangle GetVirtualScreenBounds()
    {
        int left = NativeMethods.GetSystemMetrics(NativeMethods.SmXvirtualscreen);
        int top = NativeMethods.GetSystemMetrics(NativeMethods.SmYvirtualscreen);
        int width = NativeMethods.GetSystemMetrics(NativeMethods.SmCxvirtualscreen);
        int height = NativeMethods.GetSystemMetrics(NativeMethods.SmCyvirtualscreen);
        return new System.Drawing.Rectangle(left, top, width, height);
    }

    private static class NativeMethods
    {
        internal const int SmXvirtualscreen = 76;
        internal const int SmYvirtualscreen = 77;
        internal const int SmCxvirtualscreen = 78;
        internal const int SmCyvirtualscreen = 79;

        [DllImport("user32.dll")]
        internal static extern int GetSystemMetrics(int nIndex);
    }
}
