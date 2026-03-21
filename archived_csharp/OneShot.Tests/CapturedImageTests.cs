using OneShot.Models;
using SkiaSharp;

namespace OneShot.Tests;

public sealed class CapturedImageTests
{
    [Fact]
    public void BitmapBackedImage_MaterializesPngBytes()
    {
        var image = CapturedImage.FromBitmap(CreateTaggedBitmap(6, 4), DateTimeOffset.UtcNow);

        image.PngBytes.Should().NotBeEmpty();
        image.PixelWidth.Should().Be(6);
        image.PixelHeight.Should().Be(4);
    }

    [Fact]
    public void BitmapBackedImage_RepeatedAccess_PreservesPixels()
    {
        var image = CapturedImage.FromBitmap(CreateTaggedBitmap(6, 4), DateTimeOffset.UtcNow);

        var first = ReadPixel(image, 5, 3);
        var second = ReadPixel(image, 5, 3);

        first.Should().Be(second);
        first.Should().Be(new SKColor(150, 120, 0));
    }

    private static SKBitmap CreateTaggedBitmap(int width, int height)
    {
        var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                bitmap.SetPixel(x, y, new SKColor((byte)(x * 30), (byte)(y * 40), 0));
            }
        }

        return bitmap;
    }

    private static SKColor ReadPixel(CapturedImage image, int x, int y)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        return bitmap.GetPixel(x, y);
    }
}
