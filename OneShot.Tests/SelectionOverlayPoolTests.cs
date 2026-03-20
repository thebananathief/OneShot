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
        using var pool = CreatePool(() => bounds, createdSurfaces);

        await pool.PrewarmAsync("prewarm-test");

        createdSurfaces.Should().HaveCount(2);
        createdSurfaces.Should().OnlyContain(surface => surface.ShowCount == 1 && surface.HideForPoolingCount == 1);
    }

    [Fact]
    public async Task PrewarmAsync_DoesNotMutateInUseSurface()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var bounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = CreatePool(() => bounds, createdSurfaces);
        var snapshot = new MonitorSnapshot(bounds[0], CreateCapturedImage(10, 10));

        using var lease = await pool.AcquireAsync(snapshot, "inv-1");
        var leasedSurface = (FakePooledSurface)lease.Surface;
        leasedSurface.PrepareForSnapshotCount.Should().Be(1);

        await pool.PrewarmAsync("prewarm-test");

        leasedSurface.PrepareForPrewarmCount.Should().Be(0);
        leasedSurface.ShowCount.Should().Be(0);
    }

    [Fact]
    public async Task AcquireAsync_ReusesPrewarmedSurface_ForMatchingTopology()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var bounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = CreatePool(() => bounds, createdSurfaces);

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
    public async Task ReleasingPooledSurface_ResetsBeforeHide()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var bounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = CreatePool(() => bounds, createdSurfaces);
        var snapshot = new MonitorSnapshot(bounds[0], CreateCapturedImage(10, 10));

        var lease = await pool.AcquireAsync(snapshot, "inv-1");
        var surface = createdSurfaces.Single();
        surface.ResetForPoolingCount.Should().Be(0);
        surface.HideForPoolingCount.Should().Be(0);

        lease.Dispose();

        surface.ResetForPoolingCount.Should().Be(1);
        surface.HideForPoolingCount.Should().Be(1);
        surface.OperationOrder.Should().ContainInOrder("reset", "hide");
    }

    [Fact]
    public async Task AcquireAsync_UsesTransientFallback_WhenTopologyChangesDuringActiveLease()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var monitorBounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = CreatePool(() => monitorBounds, createdSurfaces);
        var firstSnapshot = new MonitorSnapshot(monitorBounds[0], CreateCapturedImage(10, 10));

        using var activeLease = await pool.AcquireAsync(firstSnapshot, "inv-1");
        var pooledSurface = activeLease.Surface;

        monitorBounds = new[] { new System.Drawing.Rectangle(100, 0, 20, 20) };
        var changedSnapshot = new MonitorSnapshot(monitorBounds[0], CreateCapturedImage(20, 20));

        using var fallbackLease = await pool.AcquireAsync(changedSnapshot, "inv-2");

        fallbackLease.Surface.Should().NotBeSameAs(pooledSurface);
        createdSurfaces.Should().HaveCount(2);
    }

    [Fact]
    public async Task ReleaseAfterPendingRebuild_RebuildsPoolForLaterAcquire()
    {
        var createdSurfaces = new List<FakePooledSurface>();
        var monitorBounds = new[] { new System.Drawing.Rectangle(0, 0, 10, 10) };
        using var pool = CreatePool(() => monitorBounds, createdSurfaces);
        var firstSnapshot = new MonitorSnapshot(monitorBounds[0], CreateCapturedImage(10, 10));

        var activeLease = await pool.AcquireAsync(firstSnapshot, "inv-1");
        var originalSurface = activeLease.Surface;

        monitorBounds = new[] { new System.Drawing.Rectangle(100, 0, 20, 20) };
        var changedSnapshot = new MonitorSnapshot(monitorBounds[0], CreateCapturedImage(20, 20));
        using (var fallbackLease = await pool.AcquireAsync(changedSnapshot, "inv-2"))
        {
            fallbackLease.Surface.Should().NotBeSameAs(originalSurface);
        }

        activeLease.Dispose();

        using var rebuiltLease = await pool.AcquireAsync(changedSnapshot, "inv-3");
        rebuiltLease.Surface.Should().NotBeSameAs(originalSurface);
        createdSurfaces[0].CloseCount.Should().BeGreaterThan(0);
    }

    private static SelectionOverlayPool CreatePool(Func<IReadOnlyList<System.Drawing.Rectangle>> boundsProvider, ICollection<FakePooledSurface> createdSurfaces)
    {
        return new SelectionOverlayPool(
            boundsProvider,
            () =>
            {
                var surface = new FakePooledSurface();
                createdSurfaces.Add(surface);
                return surface;
            });
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

        public int PrepareForSnapshotCount { get; private set; }

        public int PrepareForPrewarmCount { get; private set; }

        public int ResetForPoolingCount { get; private set; }

        public List<string> OperationOrder { get; } = new();

        public void PrepareForSnapshot(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds)
        {
            PrepareForSnapshotCount++;
        }

        public void PrepareForPrewarm(System.Drawing.Rectangle monitorBounds)
        {
            PrepareForPrewarmCount++;
        }

        public void ResetForPooling()
        {
            ResetForPoolingCount++;
            OperationOrder.Add("reset");
        }

        public void HideForPooling()
        {
            HideForPoolingCount++;
            OperationOrder.Add("hide");
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
