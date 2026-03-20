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
        var released = 0;
        var snapshots = new[]
        {
            new MonitorSnapshot(new System.Drawing.Rectangle(0, 0, 10, 10), CreateCapturedImage(10, 10)),
            new MonitorSnapshot(new System.Drawing.Rectangle(10, 0, 10, 10), CreateCapturedImage(10, 10))
        };

        var session = new MultiMonitorSelectionSession(
            snapshots,
            overlayLeaseFactory: _ => ValueTask.FromResult(new PooledOverlayLease(new FakeSurface(), _ => released++)),
            invocationId: "inv-1",
            trace: (id, phase, _, _) => traces.Add($"{id}:{phase}"),
            installSystemHooks: false);

        var selectionTask = session.GetSelectionAsync();
        await Task.Delay(50);

        traces.Should().Contain("inv-1:overlay_show_start");
        traces.Count(t => t == "inv-1:overlay_surface_opened").Should().Be(2);
        traces.Should().Contain("inv-1:all_overlays_opened");
        traces.Should().Contain("inv-1:overlay_surface_create_start");
        released.Should().Be(0);

        selectionTask.IsCompleted.Should().BeFalse();
    }

    [Fact]
    public async Task GetSelectionAsync_ReleasesEachLeaseOnce_WhenSurfaceCloses()
    {
        var releaseCount = 0;
        FakeSurface? leasedSurface = null;
        var snapshots = new[]
        {
            new MonitorSnapshot(new System.Drawing.Rectangle(0, 0, 10, 10), CreateCapturedImage(10, 10))
        };

        var session = new MultiMonitorSelectionSession(
            snapshots,
            overlayLeaseFactory: _ =>
            {
                leasedSurface = new FakeSurface();
                return ValueTask.FromResult(new PooledOverlayLease(leasedSurface, _ => releaseCount++));
            },
            installSystemHooks: false);

        var selectionTask = session.GetSelectionAsync();
        await Task.Delay(50);
        leasedSurface.Should().NotBeNull();
        leasedSurface!.Close();

        var selection = await selectionTask;
        selection.Should().BeNull();
        releaseCount.Should().Be(1);
    }

    [Fact]
    public async Task GetSelectionAsync_WaitsForSurfaceOpenedUntilSurfaceIsVisuallyReady()
    {
        var traces = new List<string>();
        var delayedSurface = new DelayedReadySurface();
        var snapshots = new[]
        {
            new MonitorSnapshot(new System.Drawing.Rectangle(0, 0, 10, 10), CreateCapturedImage(10, 10))
        };

        var session = new MultiMonitorSelectionSession(
            snapshots,
            overlayLeaseFactory: _ => ValueTask.FromResult(new PooledOverlayLease(delayedSurface, _ => { })),
            invocationId: "inv-2",
            trace: (id, phase, _, _) => traces.Add($"{id}:{phase}"),
            installSystemHooks: false);

        var selectionTask = session.GetSelectionAsync();
        await Task.Delay(50);

        traces.Should().Contain("inv-2:overlay_show_start");
        traces.Should().NotContain("inv-2:overlay_surface_opened");
        selectionTask.IsCompleted.Should().BeFalse();

        delayedSurface.RaiseReady();
        await Task.Delay(50);

        traces.Should().Contain("inv-2:overlay_surface_opened");
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

        public bool IsPooledReusable => true;

        public void PrepareForSnapshot(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds)
        {
        }

        public void PrepareForPrewarm(System.Drawing.Rectangle monitorBounds)
        {
        }

        public void ResetForPooling()
        {
        }

        public void HideForPooling()
        {
            IsVisible = false;
        }

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

    private sealed class DelayedReadySurface : ISelectionOverlaySurface
    {
#pragma warning disable CS0067
        public event EventHandler<PixelPoint>? DragStarted;
        public event EventHandler? CancelRequested;
#pragma warning restore CS0067
        public event EventHandler? SurfaceClosed;
        public event EventHandler? SurfaceOpened;

        public bool IsVisible { get; private set; }

        public bool IsPooledReusable => true;

        public void PrepareForSnapshot(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds)
        {
        }

        public void PrepareForPrewarm(System.Drawing.Rectangle monitorBounds)
        {
        }

        public void ResetForPooling()
        {
        }

        public void HideForPooling()
        {
            IsVisible = false;
        }

        public void Show()
        {
            IsVisible = true;
        }

        public void Close()
        {
            IsVisible = false;
            SurfaceClosed?.Invoke(this, EventArgs.Empty);
        }

        public void RaiseReady()
        {
            SurfaceOpened?.Invoke(this, EventArgs.Empty);
        }

        public void SetSelection(Rect? selectionInScreenPixels)
        {
        }
    }
}
