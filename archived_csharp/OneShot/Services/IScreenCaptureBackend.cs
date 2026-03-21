using SkiaSharp;

namespace OneShot.Services;

public interface IScreenCaptureBackend
{
    SKBitmap CaptureBitmap(int x, int y, int width, int height);
}
