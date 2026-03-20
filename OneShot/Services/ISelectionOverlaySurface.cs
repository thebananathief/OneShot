using Avalonia;

namespace OneShot.Services;

internal interface ISelectionOverlaySurface
{
    event EventHandler<PixelPoint>? DragStarted;

    event EventHandler? CancelRequested;

    event EventHandler? SurfaceClosed;

    event EventHandler? SurfaceOpened;

    bool IsVisible { get; }

    void Show();

    void Close();

    void SetSelection(Rect? selectionInScreenPixels);
}
