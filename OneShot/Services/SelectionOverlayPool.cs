using System.Diagnostics;
using OneShot.Models;
using SkiaSharp;

namespace OneShot.Services;

internal sealed class SelectionOverlayPool : IDisposable
{
    private readonly Func<IReadOnlyList<System.Drawing.Rectangle>> _monitorBoundsProvider;
    private readonly Func<ISelectionOverlaySurface> _surfaceFactory;
    private readonly List<PooledSurfaceEntry> _entries = new();
    private MonitorTopologyFingerprint? _fingerprint;
    private int _rebuildCount;

    public SelectionOverlayPool(
        Func<IReadOnlyList<System.Drawing.Rectangle>> monitorBoundsProvider,
        Func<ISelectionOverlaySurface>? surfaceFactory = null)
    {
        _monitorBoundsProvider = monitorBoundsProvider;
        _surfaceFactory = surfaceFactory ?? (() => new OneShot.Windows.SelectionOverlayWindow());

    }

    public async Task PrewarmAsync(string invocationId, Action<string, string, long, object?>? trace = null)
    {
        var stopwatch = Stopwatch.StartNew();
        trace?.Invoke(invocationId, "overlay_pool_prewarm_start", stopwatch.ElapsedMilliseconds, null);
        await EnsurePoolReadyAsync(invocationId, trace, prewarm: true);
        trace?.Invoke(invocationId, "overlay_pool_prewarm_complete", stopwatch.ElapsedMilliseconds, new { PoolSize = _entries.Count, RebuildCount = _rebuildCount });
    }

    public async ValueTask<PooledOverlayLease> AcquireAsync(MonitorSnapshot snapshot, string invocationId, Action<string, string, long, object?>? trace = null)
    {
        var stopwatch = Stopwatch.StartNew();
        trace?.Invoke(invocationId, "overlay_surface_acquire_start", stopwatch.ElapsedMilliseconds, new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height });

        await EnsurePoolReadyAsync(invocationId, trace, prewarm: false);
        var entry = _entries.FirstOrDefault(candidate => !candidate.InUse && candidate.Bounds == snapshot.Bounds);
        var fromPool = entry is not null;

        if (entry is null)
        {
            entry = new PooledSurfaceEntry(snapshot.Bounds, _surfaceFactory());
        }

        entry.InUse = true;
        trace?.Invoke(invocationId, "overlay_surface_acquire_complete", stopwatch.ElapsedMilliseconds, new { FromPool = fromPool, PoolSize = _entries.Count });

        trace?.Invoke(invocationId, "overlay_surface_prepare_start", stopwatch.ElapsedMilliseconds, new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = fromPool });
        entry.Surface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
        trace?.Invoke(invocationId, "overlay_surface_prepare_complete", stopwatch.ElapsedMilliseconds, new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = fromPool });

        return new PooledOverlayLease(entry.Surface, surface => Release(entry, surface));
    }

    public void Dispose()
    {
        foreach (var entry in _entries)
        {
            entry.Surface.Close();
        }

        _entries.Clear();
    }

    private async Task EnsurePoolReadyAsync(string invocationId, Action<string, string, long, object?>? trace, bool prewarm)
    {
        var currentBounds = _monitorBoundsProvider();
        var currentFingerprint = MonitorTopologyFingerprint.Create(currentBounds);
        if (_fingerprint is null || !_fingerprint.Equals(currentFingerprint))
        {
            var stopwatch = Stopwatch.StartNew();
            trace?.Invoke(invocationId, "overlay_pool_rebuild_start", stopwatch.ElapsedMilliseconds, new { PoolSize = _entries.Count, currentBounds.Count });
            RebuildEntries(currentBounds);
            _fingerprint = currentFingerprint;
            _rebuildCount++;
            trace?.Invoke(invocationId, "overlay_pool_rebuild_complete", stopwatch.ElapsedMilliseconds, new { PoolSize = _entries.Count, RebuildCount = _rebuildCount });
        }

        if (!prewarm)
        {
            return;
        }

        foreach (var entry in _entries.Where(candidate => !candidate.IsPrewarmed))
        {
            var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

            void OnOpened(object? _, EventArgs __)
            {
                entry.Surface.SurfaceOpened -= OnOpened;
                opened.TrySetResult();
            }

            entry.Surface.SurfaceOpened += OnOpened;
            entry.Surface.PrepareForPrewarm(entry.Bounds);
            entry.Surface.Show();
            await opened.Task;
            entry.Surface.HideForPooling();
            entry.IsPrewarmed = true;
        }
    }

    private void RebuildEntries(IReadOnlyList<System.Drawing.Rectangle> currentBounds)
    {
        foreach (var entry in _entries)
        {
            entry.Surface.Close();
        }

        _entries.Clear();
        foreach (var bounds in currentBounds)
        {
            _entries.Add(new PooledSurfaceEntry(bounds, _surfaceFactory()));
        }
    }

    private void Release(PooledSurfaceEntry entry, ISelectionOverlaySurface surface)
    {
        surface.ResetForPooling();
        if (entry.Surface != surface || !_entries.Contains(entry) || !surface.IsPooledReusable)
        {
            surface.Close();
            return;
        }

        entry.InUse = false;
        surface.HideForPooling();
    }

    private sealed class PooledSurfaceEntry
    {
        public PooledSurfaceEntry(System.Drawing.Rectangle bounds, ISelectionOverlaySurface surface)
        {
            Bounds = bounds;
            Surface = surface;
        }

        public System.Drawing.Rectangle Bounds { get; }

        public ISelectionOverlaySurface Surface { get; }

        public bool InUse { get; set; }

        public bool IsPrewarmed { get; set; }
    }
}
