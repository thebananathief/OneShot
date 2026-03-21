using Avalonia;
using Avalonia.Media;
using OneShot.Models;
using OneShot.Services;
using SkiaSharp;

namespace OneShot.Tests;

public sealed class MarkupImageExportServiceTests
{
    private readonly MarkupImageExportService _service = new();

    [Fact]
    public void Export_NoPrimitives_ReturnsOriginalCapturedImageInstance()
    {
        var source = CreateCapturedImage(40, 24);

        var result = _service.Export(source, Array.Empty<MarkupPrimitive>());

        result.Should().BeSameAs(source);
        result.PngBytes.Should().Equal(source.PngBytes);
    }

    [Fact]
    public void Export_NoPrimitives_PixelsAreUnchanged()
    {
        var source = CreateCapturedImage(32, 20);

        var result = _service.Export(source, Array.Empty<MarkupPrimitive>());

        GetPixels(result).Should().Equal(GetPixels(source));
    }

    [Fact]
    public void Export_WithPenStroke_ProducesVisibleInk()
    {
        var source = CreateCapturedImage(64, 40);
        var primitives = new MarkupPrimitive[]
        {
            new PenStrokePrimitive(
                new[]
                {
                    new Point(8, 20),
                    new Point(20, 10),
                    new Point(32, 20),
                    new Point(44, 10),
                    new Point(56, 20)
                },
                Colors.Red,
                4)
        };

        var result = _service.Export(source, primitives);

        result.Should().NotBeSameAs(source);
        GetPixels(result).Should().NotEqual(GetPixels(source));
        CountStrongRedPixels(result).Should().BeGreaterThan(0);
    }

    [Fact]
    public void Export_WithOverlay_DoesNotTranslateSourceImage()
    {
        var source = CreateTaggedCornerImage(80, 60);
        var primitives = new MarkupPrimitive[]
        {
            new RectanglePrimitive(new Rect(20, 15, 24, 18), Colors.Yellow, 2, Color.FromArgb(96, 255, 255, 0))
        };

        var result = _service.Export(source, primitives);

        GetPixel(result, 0, 0).Should().Be(GetPixel(source, 0, 0));
        GetPixel(result, source.PixelWidth - 1, 0).Should().Be(GetPixel(source, source.PixelWidth - 1, 0));
        GetPixel(result, 0, source.PixelHeight - 1).Should().Be(GetPixel(source, 0, source.PixelHeight - 1));
        GetPixel(result, source.PixelWidth - 1, source.PixelHeight - 1).Should().Be(GetPixel(source, source.PixelWidth - 1, source.PixelHeight - 1));
    }

    [Fact]
    public void Export_NetEmptyEditsScenario_PassthroughMatchesNoOpWorkflow()
    {
        var source = CreateCapturedImage(50, 30);

        var result = _service.Export(source, Array.Empty<MarkupPrimitive>());

        result.Should().BeSameAs(source);
    }

    private static CapturedImage CreateCapturedImage(int width, int height)
    {
        using var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                var color = new SKColor(
                    (byte)((x * 13 + y * 2) % 255),
                    (byte)((x * 5 + y * 11) % 255),
                    (byte)((x * 7 + y * 3) % 255),
                    255);
                bitmap.SetPixel(x, y, color);
            }
        }

        return BuildCapturedImage(bitmap);
    }

    private static CapturedImage CreateTaggedCornerImage(int width, int height)
    {
        using var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(new SKColor(100, 100, 100, 255));

        bitmap.SetPixel(0, 0, new SKColor(255, 0, 0, 255));
        bitmap.SetPixel(width - 1, 0, new SKColor(0, 255, 0, 255));
        bitmap.SetPixel(0, height - 1, new SKColor(0, 0, 255, 255));
        bitmap.SetPixel(width - 1, height - 1, new SKColor(255, 255, 0, 255));

        return BuildCapturedImage(bitmap);
    }

    private static CapturedImage BuildCapturedImage(SKBitmap bitmap)
    {
        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return CapturedImage.FromPngBytes(data.ToArray(), DateTimeOffset.UtcNow);
    }

    private static byte[] GetPixels(CapturedImage image)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        return bitmap.Bytes;
    }

    private static SKColor GetPixel(CapturedImage image, int x, int y)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        return bitmap.GetPixel(x, y);
    }

    private static int CountStrongRedPixels(CapturedImage image)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        int count = 0;
        for (int y = 0; y < bitmap.Height; y++)
        {
            for (int x = 0; x < bitmap.Width; x++)
            {
                var pixel = bitmap.GetPixel(x, y);
                if (pixel.Red > 140 && pixel.Red > pixel.Green + 30 && pixel.Red > pixel.Blue + 30)
                {
                    count++;
                }
            }
        }

        return count;
    }
}