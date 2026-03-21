using Avalonia;
using OneShot.Models;

namespace OneShot.Services;

internal interface ISelectionOverlaySurface
{
    event EventHandler<PixelPoint>? DragStarted;

    event EventHandler? CancelRequested;

    event EventHandler? SurfaceClosed;

    event EventHandler? SurfaceOpened;

    bool IsVisible { get; }

    bool IsPooledReusable { get; }

    void PrepareForSnapshot(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds);

    void PrepareForPrewarm(System.Drawing.Rectangle monitorBounds);

    void ResetForPooling();

    void HideForPooling();

    void Show();

    void Close();

    void SetSelection(Rect? selectionInScreenPixels);
}
