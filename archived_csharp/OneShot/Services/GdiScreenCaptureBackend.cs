using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using SkiaSharp;

namespace OneShot.Services;

public sealed class GdiScreenCaptureBackend : IScreenCaptureBackend
{
    private static readonly nint DpiAwarenessContextPerMonitorAwareV2 = new(-4);

    public SKBitmap CaptureBitmap(int x, int y, int width, int height)
    {
        nint previousDpiContext = NativeMethods.SetThreadDpiAwarenessContext(DpiAwarenessContextPerMonitorAwareV2);
        try
        {
            using var bitmap = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
            using (var g = Graphics.FromImage(bitmap))
            {
                g.CopyFromScreen(x, y, 0, 0, bitmap.Size, CopyPixelOperation.SourceCopy);
            }

            var rect = new Rectangle(0, 0, width, height);
            var bitmapData = bitmap.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                var capturedBitmap = new SKBitmap(new SKImageInfo(width, height, SKColorType.Bgra8888, SKAlphaType.Premul));
                CopyBitmapData(bitmapData, capturedBitmap);
                return capturedBitmap;
            }
            finally
            {
                bitmap.UnlockBits(bitmapData);
            }
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

    private static void CopyBitmapData(BitmapData sourceData, SKBitmap destination)
    {
        int sourceStride = Math.Abs(sourceData.Stride);
        int destinationStride = destination.RowBytes;
        int bytesPerRow = Math.Min(sourceStride, destinationStride);
        var rowBuffer = new byte[bytesPerRow];

        for (int row = 0; row < destination.Height; row++)
        {
            IntPtr sourceRow = IntPtr.Add(sourceData.Scan0, row * sourceData.Stride);
            IntPtr destinationRow = IntPtr.Add(destination.GetPixels(), row * destinationStride);
            Marshal.Copy(sourceRow, rowBuffer, 0, bytesPerRow);
            Marshal.Copy(rowBuffer, 0, destinationRow, bytesPerRow);
        }
    }
}
