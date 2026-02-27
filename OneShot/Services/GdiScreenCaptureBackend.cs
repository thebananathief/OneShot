using System.Drawing;
using System.Drawing.Imaging;

namespace OneShot.Services;

public sealed class GdiScreenCaptureBackend : IScreenCaptureBackend
{
    public byte[] CapturePng(int x, int y, int width, int height)
    {
        using var bitmap = new Bitmap(width, height);
        using (var g = Graphics.FromImage(bitmap))
        {
            g.CopyFromScreen(x, y, 0, 0, bitmap.Size, CopyPixelOperation.SourceCopy);
        }

        using var stream = new MemoryStream();
        bitmap.Save(stream, ImageFormat.Png);
        return stream.ToArray();
    }
}
