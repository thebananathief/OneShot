using Avalonia;

namespace OneShot.Services;

internal static class NotificationPlacement
{
    internal const double DefaultWidthDip = 258;
    internal const double DefaultHeightDip = 119;

    public static NotificationPlacementResult Arrange(
        PixelRect workingArea,
        Rect measuredBoundsDip,
        double renderScaling,
        double topInPixels,
        double marginInPixels,
        double gapInPixels)
    {
        var sizePixels = MeasureSizePixels(measuredBoundsDip.Size, renderScaling);

        int desiredX = (int)Math.Round((workingArea.X + workingArea.Width) - sizePixels.Width - marginInPixels);
        int desiredY = (int)Math.Round(topInPixels);

        int minX = workingArea.X;
        int maxX = Math.Max(minX, (workingArea.X + workingArea.Width) - sizePixels.Width);
        int minY = workingArea.Y;
        int maxY = Math.Max(minY, (workingArea.Y + workingArea.Height) - sizePixels.Height);

        int x = Math.Clamp(desiredX, minX, maxX);
        int y = Math.Clamp(desiredY, minY, maxY);

        return new NotificationPlacementResult(
            new PixelPoint(x, y),
            sizePixels,
            y + sizePixels.Height + gapInPixels);
    }

    internal static PixelSize MeasureSizePixels(Size measuredSizeDip, double renderScaling)
    {
        double scaling = renderScaling > 0 ? renderScaling : 1d;
        double widthDip = measuredSizeDip.Width > 0 ? measuredSizeDip.Width : DefaultWidthDip;
        double heightDip = measuredSizeDip.Height > 0 ? measuredSizeDip.Height : DefaultHeightDip;

        return new PixelSize(
            Math.Max(1, (int)Math.Ceiling(widthDip * scaling)),
            Math.Max(1, (int)Math.Ceiling(heightDip * scaling)));
    }
}

internal readonly record struct NotificationPlacementResult(
    PixelPoint Position,
    PixelSize SizePixels,
    double NextTopInPixels);
