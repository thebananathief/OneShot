using Avalonia.Media.Imaging;
using SkiaSharp;

namespace OneShot.Models;

public sealed class CapturedImage
{
    private Bitmap? _bitmap;

    public CapturedImage(byte[] pngBytes, DateTimeOffset capturedAtUtc, int pixelWidth, int pixelHeight, Bitmap? bitmap = null)
    {
        PngBytes = pngBytes;
        CapturedAtUtc = capturedAtUtc;
        PixelWidth = pixelWidth;
        PixelHeight = pixelHeight;
        _bitmap = bitmap;
    }

    public Bitmap Bitmap => _bitmap ??= CreateBitmap(PngBytes);

    public byte[] PngBytes { get; }

    public DateTimeOffset CapturedAtUtc { get; }

    public int PixelWidth { get; }

    public int PixelHeight { get; }

    public static CapturedImage FromPngBytes(byte[] pngBytes, DateTimeOffset capturedAtUtc)
    {
        using var codec = SKCodec.Create(new SKMemoryStream(pngBytes));
        if (codec is null)
        {
            throw new InvalidOperationException("Invalid PNG payload.");
        }

        var info = codec.Info;
        return new CapturedImage(pngBytes, capturedAtUtc, info.Width, info.Height);
    }

    private static Bitmap CreateBitmap(byte[] pngBytes)
    {
        using var stream = new MemoryStream(pngBytes, writable: false);
        return new Bitmap(stream);
    }
}