using Avalonia;
using OneShot.Models;
using SkiaSharp;

namespace OneShot.Services;

public sealed class ScreenCaptureService
{
    private readonly IScreenCaptureBackend _backend;

    public ScreenCaptureService(IScreenCaptureBackend? backend = null)
    {
        _backend = backend ?? new GdiScreenCaptureBackend();
    }

    public CapturedImage Capture(Rect selection)
    {
        int x = (int)Math.Round(selection.X);
        int y = (int)Math.Round(selection.Y);
        int width = Math.Max(1, (int)Math.Round(selection.Width));
        int height = Math.Max(1, (int)Math.Round(selection.Height));

        try
        {
            byte[] bytes = _backend.CapturePng(x, y, width, height);
            return new CapturedImage(bytes, DateTimeOffset.UtcNow, width, height);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException($"Screen capture failed at ({x},{y},{width},{height}).", ex);
        }
    }

    public CapturedImage Crop(CapturedImage source, Rect selectionInSource)
    {
        int x = (int)Math.Round(selectionInSource.X);
        int y = (int)Math.Round(selectionInSource.Y);
        int width = (int)Math.Round(selectionInSource.Width);
        int height = (int)Math.Round(selectionInSource.Height);

        if (width <= 0 || height <= 0)
        {
            throw new InvalidOperationException($"Screen crop failed at ({x},{y},{width},{height}).");
        }

        int left = Math.Max(0, x);
        int top = Math.Max(0, y);
        int right = Math.Min(source.PixelWidth, x + width);
        int bottom = Math.Min(source.PixelHeight, y + height);

        if (right <= left || bottom <= top)
        {
            throw new InvalidOperationException($"Screen crop failed at ({x},{y},{width},{height}).");
        }

        int cropWidth = right - left;
        int cropHeight = bottom - top;

        try
        {
            using var sourceBitmap = SKBitmap.Decode(source.PngBytes)
                ?? throw new InvalidOperationException("Failed to decode source image for cropping.");
            using var croppedBitmap = new SKBitmap(cropWidth, cropHeight, sourceBitmap.ColorType, sourceBitmap.AlphaType);
            using (var canvas = new SKCanvas(croppedBitmap))
            {
                canvas.DrawBitmap(
                    sourceBitmap,
                    new SKRect(left, top, right, bottom),
                    new SKRect(0, 0, cropWidth, cropHeight));
            }

            using var image = SKImage.FromBitmap(croppedBitmap);
            using var data = image.Encode(SKEncodedImageFormat.Png, 100);
            return new CapturedImage(data.ToArray(), DateTimeOffset.UtcNow, cropWidth, cropHeight);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException($"Screen crop failed at ({x},{y},{width},{height}).", ex);
        }
    }
}
