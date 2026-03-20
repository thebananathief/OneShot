namespace OneShot.Services;

internal sealed class PooledOverlayLease : IDisposable
{
    private readonly Action<ISelectionOverlaySurface>? _release;
    private bool _disposed;

    public PooledOverlayLease(ISelectionOverlaySurface surface, Action<ISelectionOverlaySurface>? release)
    {
        Surface = surface;
        _release = release;
    }

    public ISelectionOverlaySurface Surface { get; }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _release?.Invoke(Surface);
    }
}
