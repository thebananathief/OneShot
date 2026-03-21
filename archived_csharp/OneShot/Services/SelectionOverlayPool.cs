using System.Diagnostics;
using Avalonia.Threading;
using OneShot.Models;

namespace OneShot.Services;

internal sealed class SelectionOverlayPool : IDisposable
{
    private readonly object _sync = new();
    private readonly Func<IReadOnlyList<System.Drawing.Rectangle>> _monitorBoundsProvider;
    private readonly Func<ISelectionOverlaySurface> _surfaceFactory;
    private readonly List<StandbyEntry> _entries = new();
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
            var entry = GetNextStandbyEntryToPrepare(invocationId, trace, stopwatch.ElapsedMilliseconds);
            if (entry is null)
            {
                trace?.Invoke(
                    invocationId,
                    "overlay_pool_prewarm_complete",
                    stopwatch.ElapsedMilliseconds,
                    new { PoolSize = GetPoolSize(), RebuildCount = GetRebuildCount() });
                return;
            }

            await PrepareStandbyEntryAsync(entry, invocationId, trace);
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

        if (TryAcquireStandby(snapshot.Bounds, invocationId, trace, out var entry, out var standbySurface))
        {
            trace?.Invoke(
                invocationId,
                "overlay_surface_acquire_complete",
                stopwatch.ElapsedMilliseconds,
                new { FromPool = true, PoolSize = GetPoolSize(), StandbyConsumed = true });

            trace?.Invoke(
                invocationId,
                "overlay_surface_prepare_start",
                stopwatch.ElapsedMilliseconds,
                new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = true, StandbyConsumed = true });
            standbySurface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
            trace?.Invoke(
                invocationId,
                "overlay_surface_prepare_complete",
                stopwatch.ElapsedMilliseconds,
                new
                {
                    snapshot.Bounds.Left,
                    snapshot.Bounds.Top,
                    snapshot.Bounds.Width,
                    snapshot.Bounds.Height,
                    FromPool = true,
                    StandbyConsumed = true,
                    SelectionVisualCleared = true
                });

            return ValueTask.FromResult(new PooledOverlayLease(standbySurface, surface => ReleaseLeasedStandby(entry, surface, invocationId, trace)));
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
            new { FromPool = false, PoolSize = GetPoolSize(), StandbyConsumed = false });
        trace?.Invoke(
            invocationId,
            "overlay_surface_prepare_start",
            stopwatch.ElapsedMilliseconds,
            new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height, FromPool = false, StandbyConsumed = false });
        fallbackSurface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
        trace?.Invoke(
            invocationId,
            "overlay_surface_prepare_complete",
            stopwatch.ElapsedMilliseconds,
            new
            {
                snapshot.Bounds.Left,
                snapshot.Bounds.Top,
                snapshot.Bounds.Width,
                snapshot.Bounds.Height,
                FromPool = false,
                StandbyConsumed = false,
                SelectionVisualCleared = true
            });

        return ValueTask.FromResult(new PooledOverlayLease(fallbackSurface, surface => surface.Close()));
    }

    public void Dispose()
    {
        foreach (var surface in ClearEntries())
        {
            surface.Close();
        }
    }

    private StandbyEntry? GetNextStandbyEntryToPrepare(string invocationId, Action<string, string, long, object?>? trace, long elapsedMs)
    {
        List<ISelectionOverlaySurface>? surfacesToClose = null;
        StandbyEntry? entry = null;

        lock (_sync)
        {
            surfacesToClose = EnsureTopologyLocked(invocationId, trace, elapsedMs);
            if (_pendingRebuild)
            {
                goto Done;
            }

            entry = _entries.FirstOrDefault(candidate => candidate.State == StandbyEntryState.Empty);
            if (entry is null)
            {
                goto Done;
            }

            entry.State = StandbyEntryState.PreparingStandby;
            entry.Surface = _surfaceFactory();
        }

Done:
        CloseSurfaces(surfacesToClose);
        return entry;
    }

    private bool TryAcquireStandby(
        System.Drawing.Rectangle bounds,
        string invocationId,
        Action<string, string, long, object?>? trace,
        out StandbyEntry entry,
        out ISelectionOverlaySurface surface)
    {
        List<ISelectionOverlaySurface>? surfacesToClose = null;
        StandbyEntry? candidate = null;
        ISelectionOverlaySurface? candidateSurface = null;

        lock (_sync)
        {
            surfacesToClose = EnsureTopologyLocked(invocationId, trace, 0);
            if (_pendingRebuild)
            {
                goto Done;
            }

            candidate = _entries.FirstOrDefault(
                candidateEntry => candidateEntry.Bounds == bounds && candidateEntry.State == StandbyEntryState.ReadyStandby && candidateEntry.Surface is not null);

            if (candidate is not null)
            {
                candidate.State = StandbyEntryState.Leased;
                candidateSurface = candidate.Surface;
                candidate.Surface = null;
            }
        }

Done:
        CloseSurfaces(surfacesToClose);
        entry = candidate!;
        surface = candidateSurface!;
        return candidate is not null && candidateSurface is not null;
    }

    private async Task PrepareStandbyEntryAsync(StandbyEntry entry, string invocationId, Action<string, string, long, object?>? trace)
    {
        var surface = entry.Surface;
        if (surface is null)
        {
            lock (_sync)
            {
                if (_entries.Contains(entry) && entry.State == StandbyEntryState.PreparingStandby)
                {
                    entry.State = StandbyEntryState.Empty;
                }
            }

            return;
        }

        var opened = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        void OnOpened(object? _, EventArgs __)
        {
            surface.SurfaceOpened -= OnOpened;
            opened.TrySetResult();
        }

        surface.SurfaceOpened += OnOpened;
        surface.PrepareForPrewarm(entry.Bounds);
        surface.Show();
        await opened.Task;
        surface.HideForPooling();

        lock (_sync)
        {
            if (!_entries.Contains(entry))
            {
                surface.Close();
                return;
            }

            if (entry.State == StandbyEntryState.PreparingStandby && entry.Surface == surface)
            {
                entry.State = StandbyEntryState.ReadyStandby;
            }
        }
    }

    private void ReleaseLeasedStandby(StandbyEntry entry, ISelectionOverlaySurface surface, string invocationId, Action<string, string, long, object?>? trace)
    {
        List<ISelectionOverlaySurface>? surfacesToClose;
        bool queueReplacement;
        StandbyEntry? preparedEntry;

        lock (_sync)
        {
            queueReplacement = _entries.Contains(entry) && entry.State == StandbyEntryState.Leased;
            entry.State = StandbyEntryState.Empty;
            entry.Surface = null;
            surfacesToClose = TryConsumePendingRebuildLocked();
        }

        surface.Close();

        trace?.Invoke(
            invocationId,
            "overlay_standby_replacement_start",
            0,
            new { ReplacementQueued = queueReplacement, Bounds = entry.Bounds });

        CloseSurfaces(surfacesToClose);

        if (!queueReplacement)
        {
            return;
        }

        preparedEntry = GetNextEntryForReplacement(entry.Bounds);
        if (preparedEntry is null)
        {
            trace?.Invoke(
                invocationId,
                "overlay_standby_replacement_complete",
                0,
                new { ReplacementReady = false, Bounds = entry.Bounds });
            return;
        }

        if (Dispatcher.UIThread.CheckAccess())
        {
            _ = CompleteReplacementAsync(preparedEntry, invocationId, trace);
            return;
        }

        _ = Dispatcher.UIThread.InvokeAsync(
            () => CompleteReplacementAsync(preparedEntry, invocationId, trace),
            DispatcherPriority.Background);
    }

    private StandbyEntry? GetNextEntryForReplacement(System.Drawing.Rectangle bounds)
    {
        lock (_sync)
        {
            if (_pendingRebuild)
            {
                return null;
            }

            var entry = _entries.FirstOrDefault(candidate => candidate.Bounds == bounds && candidate.State == StandbyEntryState.Empty && candidate.Surface is null);
            if (entry is null)
            {
                return null;
            }

            entry.State = StandbyEntryState.PreparingStandby;
            entry.Surface = _surfaceFactory();
            return entry;
        }
    }

    private async Task CompleteReplacementAsync(StandbyEntry preparedEntry, string invocationId, Action<string, string, long, object?>? trace)
    {
        var stopwatch = Stopwatch.StartNew();
        await PrepareStandbyEntryAsync(preparedEntry, invocationId, trace);
        bool replacementReady;
        lock (_sync)
        {
            replacementReady = _entries.Contains(preparedEntry) && preparedEntry.State == StandbyEntryState.ReadyStandby;
        }

        trace?.Invoke(
            invocationId,
            "overlay_standby_replacement_complete",
            stopwatch.ElapsedMilliseconds,
            new { ReplacementReady = replacementReady, Bounds = preparedEntry.Bounds });
    }

    private List<ISelectionOverlaySurface>? EnsureTopologyLocked(string invocationId, Action<string, string, long, object?>? trace, long elapsedMs)
    {
        var currentBounds = _monitorBoundsProvider();
        var currentFingerprint = MonitorTopologyFingerprint.Create(currentBounds);
        if (_fingerprint is not null && _fingerprint.Equals(currentFingerprint))
        {
            return null;
        }

        if (_entries.Any(entry => entry.State == StandbyEntryState.Leased || entry.State == StandbyEntryState.PreparingStandby))
        {
            _pendingRebuild = true;
            trace?.Invoke(
                invocationId,
                "overlay_pool_pending_rebuild",
                elapsedMs,
                new { PoolSize = _entries.Count, currentBounds.Count });
            return null;
        }

        trace?.Invoke(
            invocationId,
            "overlay_pool_rebuild_start",
            elapsedMs,
            new { PoolSize = _entries.Count, currentBounds.Count });

        var oldSurfaces = _entries
            .Select(entry => entry.Surface)
            .Where(surface => surface is not null)
            .Cast<ISelectionOverlaySurface>()
            .ToList();
        _entries.Clear();
        foreach (var bounds in currentBounds)
        {
            _entries.Add(new StandbyEntry(bounds));
        }

        _fingerprint = currentFingerprint;
        _pendingRebuild = false;
        _rebuildCount++;

        trace?.Invoke(
            invocationId,
            "overlay_pool_rebuild_complete",
            elapsedMs,
            new { PoolSize = _entries.Count, RebuildCount = _rebuildCount });
        return oldSurfaces;
    }

    private List<ISelectionOverlaySurface>? TryConsumePendingRebuildLocked()
    {
        if (!_pendingRebuild || _entries.Any(entry => entry.State == StandbyEntryState.Leased || entry.State == StandbyEntryState.PreparingStandby))
        {
            return null;
        }

        var oldSurfaces = _entries
            .Select(entry => entry.Surface)
            .Where(surface => surface is not null)
            .Cast<ISelectionOverlaySurface>()
            .ToList();
        _entries.Clear();
        _fingerprint = null;
        _pendingRebuild = false;
        return oldSurfaces;
    }

    private List<ISelectionOverlaySurface> ClearEntries()
    {
        lock (_sync)
        {
            var surfaces = _entries
                .Select(entry => entry.Surface)
                .Where(surface => surface is not null)
                .Cast<ISelectionOverlaySurface>()
                .ToList();
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

    private sealed class StandbyEntry
    {
        public StandbyEntry(System.Drawing.Rectangle bounds)
        {
            Bounds = bounds;
        }

        public System.Drawing.Rectangle Bounds { get; }

        public ISelectionOverlaySurface? Surface { get; set; }

        public StandbyEntryState State { get; set; }
    }

    private enum StandbyEntryState
    {
        Empty,
        PreparingStandby,
        ReadyStandby,
        Leased
    }
}
