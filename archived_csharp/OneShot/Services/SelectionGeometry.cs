using Avalonia;

namespace OneShot.Services;

internal static class SelectionGeometry
{
    public static Rect BuildRect(PixelPoint a, PixelPoint b)
    {
        int left = Math.Min(a.X, b.X);
        int top = Math.Min(a.Y, b.Y);
        int width = Math.Abs(a.X - b.X);
        int height = Math.Abs(a.Y - b.Y);
        return new Rect(left, top, width, height);
    }

    public static bool MeetsMinimumSize(Rect rect, int minWidth, int minHeight)
    {
        return rect.Width >= minWidth && rect.Height >= minHeight;
    }

    public static Rect? IntersectWithMonitor(Rect selectionInScreenPixels, System.Drawing.Rectangle monitorBounds)
    {
        double left = Math.Max(selectionInScreenPixels.X, monitorBounds.Left);
        double top = Math.Max(selectionInScreenPixels.Y, monitorBounds.Top);
        double right = Math.Min(selectionInScreenPixels.Right, monitorBounds.Right);
        double bottom = Math.Min(selectionInScreenPixels.Bottom, monitorBounds.Bottom);
        double width = right - left;
        double height = bottom - top;
        if (width <= 0 || height <= 0)
        {
            return null;
        }

        return new Rect(left, top, width, height);
    }
}
