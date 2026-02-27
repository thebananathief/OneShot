using Avalonia;
using FluentAssertions;
using OneShot.Models;
using OneShot.Services;
using SkiaSharp;

namespace OneShot.Tests;

public sealed class ScreenCaptureServiceTests
{
    [Fact]
    public void Capture_UsesBackendBoundsAndReturnsImageMetadata()
    {
        var backend = new FakeScreenCaptureBackend();
        var service = new ScreenCaptureService(backend);
        var selection = new Rect(10.2, 11.8, 99.4, 50.6);

        var image = service.Capture(selection);

        backend.LastRequest.Should().BeEquivalentTo((10, 12, 99, 51));
        image.PixelWidth.Should().Be(99);
        image.PixelHeight.Should().Be(51);
        image.PngBytes.Should().NotBeEmpty();
    }

    [Fact]
    public void Capture_WhenBackendFails_ThrowsContextualException()
    {
        var backend = new FailingScreenCaptureBackend();
        var service = new ScreenCaptureService(backend);

        var act = () => service.Capture(new Rect(1, 2, 3, 4));

        act.Should().Throw<InvalidOperationException>()
            .WithMessage("Screen capture failed*");
    }

    [Fact]
    public void Crop_ValidRect_ReturnsExpectedDimensionsAndPixels()
    {
        var service = new ScreenCaptureService(new FakeScreenCaptureBackend());
        var source = CreateTaggedImage(8, 6);

        var cropped = service.Crop(source, new Rect(2, 1, 4, 3));

        cropped.PixelWidth.Should().Be(4);
        cropped.PixelHeight.Should().Be(3);
        ReadPixel(cropped, 0, 0).Should().Be(new SKColor(60, 40, 0));
        ReadPixel(cropped, 3, 2).Should().Be(new SKColor(150, 120, 0));
    }

    [Fact]
    public void Crop_RectPartiallyOutside_ClampsToBounds()
    {
        var service = new ScreenCaptureService(new FakeScreenCaptureBackend());
        var source = CreateTaggedImage(8, 6);

        var cropped = service.Crop(source, new Rect(6, 4, 10, 10));

        cropped.PixelWidth.Should().Be(2);
        cropped.PixelHeight.Should().Be(2);
        ReadPixel(cropped, 0, 0).Should().Be(new SKColor(180, 160, 0));
    }

    [Fact]
    public void Crop_RectOutside_ThrowsContextualException()
    {
        var service = new ScreenCaptureService(new FakeScreenCaptureBackend());
        var source = CreateTaggedImage(8, 6);

        var act = () => service.Crop(source, new Rect(20, 10, 5, 5));

        act.Should().Throw<InvalidOperationException>()
            .WithMessage("Screen crop failed*");
    }

    [Fact]
    public void Crop_RoundingBehavior_IsDeterministic()
    {
        var service = new ScreenCaptureService(new FakeScreenCaptureBackend());
        var source = CreateTaggedImage(8, 6);

        var cropped = service.Crop(source, new Rect(1.6, 1.4, 2.6, 2.2));

        cropped.PixelWidth.Should().Be(3);
        cropped.PixelHeight.Should().Be(2);
        ReadPixel(cropped, 0, 0).Should().Be(new SKColor(60, 40, 0));
    }

    private static CapturedImage CreateTaggedImage(int width, int height)
    {
        using var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
        using (var canvas = new SKCanvas(bitmap))
        {
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    bitmap.SetPixel(x, y, new SKColor((byte)(x * 30), (byte)(y * 40), 0));
                }
            }

            canvas.Flush();
        }

        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return CapturedImage.FromPngBytes(data.ToArray(), DateTimeOffset.UtcNow);
    }

    private static SKColor ReadPixel(CapturedImage image, int x, int y)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        return bitmap.GetPixel(x, y);
    }

    private sealed class FakeScreenCaptureBackend : IScreenCaptureBackend
    {
        public (int X, int Y, int Width, int Height) LastRequest { get; private set; }

        public byte[] CapturePng(int x, int y, int width, int height)
        {
            LastRequest = (x, y, width, height);
            using var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
            using var canvas = new SKCanvas(bitmap);
            canvas.Clear(SKColors.OrangeRed);
            using var image = SKImage.FromBitmap(bitmap);
            using var data = image.Encode(SKEncodedImageFormat.Png, 100);
            return data.ToArray();
        }
    }

    private sealed class FailingScreenCaptureBackend : IScreenCaptureBackend
    {
        public byte[] CapturePng(int x, int y, int width, int height)
        {
            throw new IOException("Backend failure");
        }
    }
}
