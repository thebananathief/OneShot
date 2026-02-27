using Avalonia.Input;
using FluentAssertions;
using OneShot.Models;
using OneShot.Services;
using SkiaSharp;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace OneShot.Tests;

public sealed class OutputServiceTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "OneShot.Tests", Guid.NewGuid().ToString("N"));
    private readonly string _saveDir;
    private readonly string _tempDir;

    public OutputServiceTests()
    {
        _saveDir = Path.Combine(_root, "save");
        _tempDir = Path.Combine(_root, "temp");
    }

    [Fact]
    public async Task SaveAsync_WritesPngToConfiguredFolder()
    {
        var service = new OutputService(_saveDir, _tempDir);
        var image = CreateCapturedImage(16, 12);

        string path = await service.SaveAsync(image);

        File.Exists(path).Should().BeTrue();
        Path.GetDirectoryName(path).Should().Be(_saveDir);
        var bytes = await File.ReadAllBytesAsync(path);
        bytes.Should().Equal(image.PngBytes);
    }

    [Fact]
    public async Task BuildDragDataAsync_UsesTempFileAndFileDropPayload()
    {
        var service = new OutputService(_saveDir, _tempDir);
        var image = CreateCapturedImage(20, 14);

        IDataObject data = await service.BuildDragDataAsync(image);

        var text = data.Get(DataFormats.Text) as string;
        text.Should().NotBeNullOrWhiteSpace();
        File.Exists(text!).Should().BeTrue();

        var fileNames = data.Get(DataFormats.FileNames) as IEnumerable<string>;
        fileNames.Should().NotBeNull();
        fileNames!.Should().ContainSingle().Which.Should().Be(text);
    }

    public void Dispose()
    {
        if (Directory.Exists(_root))
        {
            Directory.Delete(_root, recursive: true);
        }
    }

    private static CapturedImage CreateCapturedImage(int width, int height)
    {
        using var bitmap = new SKBitmap(width, height, SKColorType.Bgra8888, SKAlphaType.Premul);
        using var canvas = new SKCanvas(bitmap);
        canvas.Clear(SKColors.CornflowerBlue);
        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return CapturedImage.FromPngBytes(data.ToArray(), DateTimeOffset.UtcNow);
    }
}
