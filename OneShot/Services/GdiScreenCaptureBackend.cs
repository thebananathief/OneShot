using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace OneShot.Services;

public sealed class GdiScreenCaptureBackend : IScreenCaptureBackend
{
    private static readonly nint DpiAwarenessContextPerMonitorAwareV2 = new(-4);

    public byte[] CapturePng(int x, int y, int width, int height)
    {
        nint previousDpiContext = NativeMethods.SetThreadDpiAwarenessContext(DpiAwarenessContextPerMonitorAwareV2);
        try
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
        finally
        {
            if (previousDpiContext != nint.Zero)
            {
                NativeMethods.SetThreadDpiAwarenessContext(previousDpiContext);
            }
        }
    }

    private static class NativeMethods
    {
        [DllImport("user32.dll")]
        internal static extern nint SetThreadDpiAwarenessContext(nint dpiContext);
    }
}
