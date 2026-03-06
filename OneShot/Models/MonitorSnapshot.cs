namespace OneShot.Models;

internal sealed class MonitorSnapshot
{
    public MonitorSnapshot(System.Drawing.Rectangle bounds, CapturedImage image)
    {
        Bounds = bounds;
        Image = image;
    }

    public System.Drawing.Rectangle Bounds { get; }

    public CapturedImage Image { get; }
}
