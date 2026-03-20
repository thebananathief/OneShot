using Avalonia;
using FluentAssertions.Execution;
using OneShot.Models;
using OneShot.Services;
using SkiaSharp;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace OneShot.Tests;

public sealed class SnapshotCoordinatorTests
{
    [Fact]
    public async Task StartSnapshotAsync_CropsFrozenVirtualCaptureInsteadOfTakingSecondLiveCapture()
    {
        var backend = new TaggedScreenCaptureBackend();
        var captureService = new ScreenCaptureService(backend);
        CapturedImage? deliveredImage = null;
        IReadOnlyList<MonitorSnapshot>? deliveredSnapshots = null;

        var coordinator = new SnapshotCoordinator(
            captureService,
            image => deliveredImage = image,
            log: null,
            monitorBoundsProvider: () => new[]
            {
                new System.Drawing.Rectangle(0, 0, 4, 3)
            },
            selectionProvider: (_, snapshots, _) =>
            {
                deliveredSnapshots = snapshots.ToArray();
                return Task.FromResult<Rect?>(new Rect(1, 1, 2, 1));
            });

        await coordinator.StartSnapshotAsync("test-invocation");

        var snapshots = deliveredSnapshots;
        var finalImage = deliveredImage;
        var actualSnapshots = snapshots!;
        var actualImage = finalImage!;

        using (new AssertionScope())
        {
            backend.Requests.Should().ContainSingle().Which.Should().Be((0, 0, 4, 3));
            snapshots.Should().NotBeNull();
            actualSnapshots.Should().ContainSingle();
            actualSnapshots[0].Image.PixelWidth.Should().Be(4);
            actualSnapshots[0].Image.PixelHeight.Should().Be(3);
            finalImage.Should().NotBeNull();
            actualImage.PixelWidth.Should().Be(2);
            actualImage.PixelHeight.Should().Be(1);
            ReadPixel(actualImage, 0, 0).Should().Be(new SKColor(40, 60, 0));
            ReadPixel(actualImage, 1, 0).Should().Be(new SKColor(80, 60, 0));
        }
    }

    private static SKColor ReadPixel(CapturedImage image, int x, int y)
    {
        using var bitmap = SKBitmap.Decode(image.PngBytes);
        return bitmap.GetPixel(x, y);
    }

    private sealed class TaggedScreenCaptureBackend : IScreenCaptureBackend
    {
        public List<(int X, int Y, int Width, int Height)> Requests { get; } = new();

        public SKBitmap CaptureBitmap(int x, int y, int width, int height)
        {
            Requests.Add((x, y, width, height));

            var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
            for (int row = 0; row < height; row++)
            {
                for (int col = 0; col < width; col++)
                {
                    bitmap.SetPixel(col, row, new SKColor((byte)((x + col) * 40), (byte)((y + row) * 60), 0));
                }
            }

            return bitmap;
        }
    }
}
