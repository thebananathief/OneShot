using Avalonia;
using OneShot.Models;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class MultiMonitorSelectionSessionTests
{
    [Fact]
    public async Task GetSelectionAsync_RecordsOverlayOpenMilestones()
    {
        var traces = new List<string>();
        var snapshots = new[]
        {
            new MonitorSnapshot(new System.Drawing.Rectangle(0, 0, 10, 10), CreateCapturedImage(10, 10)),
            new MonitorSnapshot(new System.Drawing.Rectangle(10, 0, 10, 10), CreateCapturedImage(10, 10))
        };

        var session = new MultiMonitorSelectionSession(
            snapshots,
            surfaceFactory: _ => new FakeSurface(),
            invocationId: "inv-1",
            trace: (id, phase, _, _) => traces.Add($"{id}:{phase}"),
            installSystemHooks: false);

        var selectionTask = session.GetSelectionAsync();
        await Task.Delay(50);

        traces.Should().Contain("inv-1:overlay_show_start");
        traces.Count(t => t == "inv-1:overlay_surface_opened").Should().Be(2);
        traces.Should().Contain("inv-1:all_overlays_opened");

        selectionTask.IsCompleted.Should().BeFalse();
    }

    private static CapturedImage CreateCapturedImage(int width, int height)
    {
        var bitmap = new SkiaSharp.SKBitmap(width, height, SkiaSharp.SKColorType.Bgra8888, SkiaSharp.SKAlphaType.Premul);
        return CapturedImage.FromBitmap(bitmap, DateTimeOffset.UtcNow);
    }

    private sealed class FakeSurface : ISelectionOverlaySurface
    {
#pragma warning disable CS0067
        public event EventHandler<PixelPoint>? DragStarted;
        public event EventHandler? CancelRequested;
#pragma warning restore CS0067
        public event EventHandler? SurfaceClosed;
        public event EventHandler? SurfaceOpened;

        public bool IsVisible { get; private set; }

        public void Show()
        {
            IsVisible = true;
            SurfaceOpened?.Invoke(this, EventArgs.Empty);
        }

        public void Close()
        {
            IsVisible = false;
            SurfaceClosed?.Invoke(this, EventArgs.Empty);
        }

        public void SetSelection(Rect? selectionInScreenPixels)
        {
        }
    }
}
