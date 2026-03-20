using Avalonia;
using OneShot.Models;
using OneShot.Services;
using SkiaSharp;

namespace OneShot.Tests;

public sealed class SelectionOverlayPoolTests
{
    [Fact]
    public async Task PrewarmAsync_CreatesOneReusableSurfacePerMonitor()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var bounds = new[]
        {
            new System.Drawing.Rectangle(0, 0, 10, 10),
            new System.Drawing.Rectangle(10, 0, 10, 10)
        };
        using var pool = new SelectionOverlayPool(
            () => bounds,
            () =>
            {
                var surface = new FakePooledSurface();
                createdSurfaces.Add(surface);
                return surface;
            });

        await pool.PrewarmAsync("prewarm-test");

        createdSurfaces.Should().HaveCount(2);
        createdSurfaces.Should().OnlyContain(surface => surface.ShowCount == 1 && surface.HideForPoolingCount == 1);
    }

    [Fact]
    public async Task AcquireAsync_ReusesPrewarmedSurface_ForMatchingTopology()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var bounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = new SelectionOverlayPool(
            () => bounds,
            () =>
            {
                var surface = new FakePooledSurface();
                createdSurfaces.Add(surface);
                return surface;
            });

        await pool.PrewarmAsync("prewarm-test");
        var snapshot = new MonitorSnapshot(bounds[0], CreateCapturedImage(10, 10));

        ISelectionOverlaySurface firstSurface;
        using (var lease = await pool.AcquireAsync(snapshot, "inv-1"))
        {
            firstSurface = lease.Surface;
        }

        using var secondLease = await pool.AcquireAsync(snapshot, "inv-2");
        secondLease.Surface.Should().BeSameAs(firstSurface);
        createdSurfaces.Should().HaveCount(1);
    }

    [Fact]
    public async Task AcquireAsync_RebuildsPool_WhenTopologyChanges()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var monitorBounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = new SelectionOverlayPool(
            () => monitorBounds,
            () =>
            {
                var surface = new FakePooledSurface();
                createdSurfaces.Add(surface);
                return surface;
            });

        await pool.PrewarmAsync("prewarm-test");
        createdSurfaces.Should().HaveCount(1);

        var originalSurface = createdSurfaces[0];
        monitorBounds = new[] { new System.Drawing.Rectangle(100, 0, 20, 20) };
        var snapshot = new MonitorSnapshot(monitorBounds[0], CreateCapturedImage(20, 20));

        using var lease = await pool.AcquireAsync(snapshot, "inv-2");

        createdSurfaces.Should().HaveCount(2);
        originalSurface.CloseCount.Should().BeGreaterThan(0);
        lease.Surface.Should().NotBeSameAs(originalSurface);
    }

    private static CapturedImage CreateCapturedImage(int width, int height)
    {
        var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
        return CapturedImage.FromBitmap(bitmap, DateTimeOffset.UtcNow);
    }

    private sealed class FakePooledSurface : ISelectionOverlaySurface
    {
#pragma warning disable CS0067
        public event EventHandler<PixelPoint>? DragStarted;
        public event EventHandler? CancelRequested;
#pragma warning restore CS0067
        public event EventHandler? SurfaceClosed;
        public event EventHandler? SurfaceOpened;

        public bool IsVisible { get; private set; }

        public bool IsPooledReusable => true;

        public int ShowCount { get; private set; }

        public int CloseCount { get; private set; }

        public int HideForPoolingCount { get; private set; }

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
            HideForPoolingCount++;
            IsVisible = false;
        }

        public void Show()
        {
            ShowCount++;
            IsVisible = true;
            SurfaceOpened?.Invoke(this, EventArgs.Empty);
        }

        public void Close()
        {
            CloseCount++;
            IsVisible = false;
            SurfaceClosed?.Invoke(this, EventArgs.Empty);
        }

        public void SetSelection(Rect? selectionInScreenPixels)
        {
        }
    }
}
