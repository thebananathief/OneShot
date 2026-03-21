using Avalonia;

namespace OneShot.Services;

internal static class SnapshotSelectionMapper
{
    public static Rect ToVirtualCaptureRect(Rect selectionInScreenPixels, System.Drawing.Rectangle virtualScreenBounds)
    {
        return new Rect(
            selectionInScreenPixels.X - virtualScreenBounds.Left,
            selectionInScreenPixels.Y - virtualScreenBounds.Top,
            selectionInScreenPixels.Width,
            selectionInScreenPixels.Height);
    }
}
