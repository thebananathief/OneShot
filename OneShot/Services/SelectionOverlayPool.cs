using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

internal sealed class SelectionOverlayPool : IDisposable
{
    private readonly object _sync = new();
    private readonly Func<IReadOnlyList<System.Drawing.Rectangle>> _monitorBoundsProvider;
    private readonly Func<ISelectionOverlaySurface> _surfaceFactory;
    private readonly List<PooledSurfaceEntry> _entries = new();
    private MonitorTopologyFingerprint? _fingerprint;
    private bool _pendingRebuild;
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

        while (true)
        {
            var preparation = PrepareNextPrewarmEntry(invocationId, trace, stopwatch);
            if (preparation is null)
            {
                trace?.Invoke(
                    invocationId,
                    "overlay_pool_prewarm_complete",
                    stopwatch.ElapsedMilliseconds,
                    new { PoolSize = GetPoolSize(), RebuildCount = GetRebuildCount() });
                return;
            }

            var (entry, warmed) = preparation.Value;
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
            warmed.Dispose();
        }
    }

    public ValueTask<PooledOverlayLease> AcquireAsync(MonitorSnapshot snapshot, string invocationId, Action<string, string, long, object?>? trace = null)
    {
        var stopwatch = Stopwatch.StartNew();
        trace?.Invoke(
            invocationId,
            "overlay_surface_acquire_start",
            stopwatch.ElapsedMilliseconds,
            new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height });

        if (TryAcquirePooledEntry(snapshot.Bounds, invocationId, trace, out var pooledEntry))
        {
            trace?.Invoke(
                invocationId,
                "overlay_surface_acquire_complete",
                stopwatch.ElapsedMilliseconds,
                new { FromPool = true, PoolSize = GetPoolSize() });

            trace?.Invoke(
                invocationId,
                "overlay_surface_prepare_start",
                stopwatch.ElapsedMilliseconds,
                new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = true });
            pooledEntry.Surface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
            trace?.Invoke(
                invocationId,
                "overlay_surface_prepare_complete",
                stopwatch.ElapsedMilliseconds,
                new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = true });

            return ValueTask.FromResult(new PooledOverlayLease(pooledEntry.Surface, _ => ReleasePooledEntry(pooledEntry)));
        }

        trace?.Invoke(
            invocationId,
            "overlay_surface_fallback_created",
            stopwatch.ElapsedMilliseconds,
            new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height });

        var fallbackSurface = _surfaceFactory();
        trace?.Invoke(
            invocationId,
            "overlay_surface_acquire_complete",
            stopwatch.ElapsedMilliseconds,
            new { FromPool = false, PoolSize = GetPoolSize() });
        trace?.Invoke(
            invocationId,
            "overlay_surface_prepare_start",
            stopwatch.ElapsedMilliseconds,
            new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = false });
        fallbackSurface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
        trace?.Invoke(
            invocationId,
            "overlay_surface_prepare_complete",
            stopwatch.ElapsedMilliseconds,
            new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = false });

        return ValueTask.FromResult(new PooledOverlayLease(fallbackSurface, surface => surface.Close()));
    }

    public void Dispose()
    {
        foreach (var surface in ClearEntries())
        {
            surface.Close();
        }
    }

    private (PooledSurfaceEntry Entry, PooledOverlayLease Lease)? PrepareNextPrewarmEntry(
        string invocationId,
        Action<string, string, long, object?>? trace,
        Stopwatch stopwatch)
    {
        List<ISelectionOverlaySurface>? surfacesToClose = null;
        PooledSurfaceEntry? entry = null;
        lock (_sync)
        {
            surfacesToClose = EnsureTopologyLocked(invocationId, trace, stopwatch);
            if (_pendingRebuild)
            {
                goto Done;
            }

            entry = _entries.FirstOrDefault(candidate => candidate.State == PooledEntryState.Idle && !candidate.IsPrewarmed);
            if (entry is null)
            {
                goto Done;
            }

            entry.State = PooledEntryState.Prewarming;
        }

Done:
        CloseSurfaces(surfacesToClose);
        return entry is null
            ? null
            : (entry, new PooledOverlayLease(entry.Surface, _ => ReleasePrewarmedEntry(entry)));
    }

    private bool TryAcquirePooledEntry(
        System.Drawing.Rectangle bounds,
        string invocationId,
        Action<string, string, long, object?>? trace,
        out PooledSurfaceEntry entry)
    {
        List<ISelectionOverlaySurface>? surfacesToClose = null;
        PooledSurfaceEntry? candidate = null;
        lock (_sync)
        {
            surfacesToClose = EnsureTopologyLocked(invocationId, trace, null);
            if (_pendingRebuild)
            {
                goto Done;
            }

            candidate = _entries.FirstOrDefault(candidateEntry => candidateEntry.Bounds == bounds && candidateEntry.State == PooledEntryState.Idle);
            if (candidate is not null)
            {
                candidate.State = PooledEntryState.InUse;
                candidate.IsPrewarmed = true;
            }
        }

Done:
        CloseSurfaces(surfacesToClose);
        entry = candidate!;
        return candidate is not null;
    }

    private void ReleasePrewarmedEntry(PooledSurfaceEntry entry)
    {
        if (!entry.Surface.IsPooledReusable)
        {
            entry.Surface.Close();
            return;
        }

        entry.Surface.HideForPooling();

        List<ISelectionOverlaySurface>? surfacesToClose = null;
        lock (_sync)
        {
            if (!_entries.Contains(entry))
            {
                return;
            }

            entry.State = PooledEntryState.Idle;
            entry.IsPrewarmed = true;
            surfacesToClose = TryConsumePendingRebuildLocked();
        }

        CloseSurfaces(surfacesToClose);
    }

    private void ReleasePooledEntry(PooledSurfaceEntry entry)
    {
        if (!entry.Surface.IsPooledReusable)
        {
            entry.Surface.Close();
            return;
        }

        entry.Surface.ResetForPooling();
        entry.Surface.HideForPooling();

        List<ISelectionOverlaySurface>? surfacesToClose = null;
        lock (_sync)
        {
            if (!_entries.Contains(entry))
            {
                return;
            }

            entry.State = PooledEntryState.Idle;
            entry.IsPrewarmed = true;
            surfacesToClose = TryConsumePendingRebuildLocked();
        }

        CloseSurfaces(surfacesToClose);
    }

    private List<ISelectionOverlaySurface>? EnsureTopologyLocked(string invocationId, Action<string, string, long, object?>? trace, Stopwatch? stopwatch)
    {
        var currentBounds = _monitorBoundsProvider();
        var currentFingerprint = MonitorTopologyFingerprint.Create(currentBounds);
        if (_fingerprint is not null && _fingerprint.Equals(currentFingerprint))
        {
            return null;
        }

        if (_entries.Any(entry => entry.State == PooledEntryState.InUse || entry.State == PooledEntryState.Prewarming))
        {
            _pendingRebuild = true;
            trace?.Invoke(
                invocationId,
                "overlay_pool_pending_rebuild",
                stopwatch?.ElapsedMilliseconds ?? 0,
                new { PoolSize = _entries.Count, currentBounds.Count });
            return null;
        }

        trace?.Invoke(
            invocationId,
            "overlay_pool_rebuild_start",
            stopwatch?.ElapsedMilliseconds ?? 0,
            new { PoolSize = _entries.Count, currentBounds.Count });

        var oldSurfaces = _entries.Select(entry => entry.Surface).ToList();
        _entries.Clear();
        foreach (var bounds in currentBounds)
        {
            _entries.Add(new PooledSurfaceEntry(bounds, _surfaceFactory()));
        }

        _fingerprint = currentFingerprint;
        _pendingRebuild = false;
        _rebuildCount++;

        trace?.Invoke(
            invocationId,
            "overlay_pool_rebuild_complete",
            stopwatch?.ElapsedMilliseconds ?? 0,
            new { PoolSize = _entries.Count, RebuildCount = _rebuildCount });
        return oldSurfaces;
    }

    private List<ISelectionOverlaySurface>? TryConsumePendingRebuildLocked()
    {
        if (!_pendingRebuild || _entries.Any(entry => entry.State == PooledEntryState.InUse || entry.State == PooledEntryState.Prewarming))
        {
            return null;
        }

        var oldSurfaces = _entries.Select(entry => entry.Surface).ToList();
        _entries.Clear();
        _fingerprint = null;
        _pendingRebuild = false;
        return oldSurfaces;
    }

    private List<ISelectionOverlaySurface> ClearEntries()
    {
        lock (_sync)
        {
            var surfaces = _entries.Select(entry => entry.Surface).ToList();
            _entries.Clear();
            _fingerprint = null;
            _pendingRebuild = false;
            return surfaces;
        }
    }

    private int GetPoolSize()
    {
        lock (_sync)
        {
            return _entries.Count;
        }
    }

    private int GetRebuildCount()
    {
        lock (_sync)
        {
            return _rebuildCount;
        }
    }

    private static void CloseSurfaces(IReadOnlyList<ISelectionOverlaySurface>? surfaces)
    {
        if (surfaces is null)
        {
            return;
        }

        foreach (var surface in surfaces)
        {
            surface.Close();
        }
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

        public PooledEntryState State { get; set; }

        public bool IsPrewarmed { get; set; }
    }

    private enum PooledEntryState
    {
        Idle,
        InUse,
        Prewarming
    }
}
