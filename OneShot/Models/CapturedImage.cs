using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using SkiaSharp;

namespace OneShot.Models;

public sealed class CapturedImage
{
    private readonly object _sync = new();
    private Bitmap? _bitmap;
    private byte[]? _pngBytes;
    private SKBitmap? _skBitmap;

    public CapturedImage(byte[] pngBytes, DateTimeOffset capturedAtUtc, int pixelWidth, int pixelHeight, Bitmap? bitmap = null)
    {
        _pngBytes = pngBytes;
        CapturedAtUtc = capturedAtUtc;
        PixelWidth = pixelWidth;
        PixelHeight = pixelHeight;
        _bitmap = bitmap;
    }

    private CapturedImage(SKBitmap skBitmap, DateTimeOffset capturedAtUtc)
    {
        _skBitmap = skBitmap;
        CapturedAtUtc = capturedAtUtc;
        PixelWidth = skBitmap.Width;
        PixelHeight = skBitmap.Height;
    }

    public Bitmap Bitmap
    {
        get
        {
            lock (_sync)
            {
                return _bitmap ??= CreateBitmap(AsSkBitmap());
            }
        }
    }

    public byte[] PngBytes
    {
        get
        {
            lock (_sync)
            {
                return _pngBytes ??= CreatePngBytes(AsSkBitmap());
            }
        }
    }

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

    public static CapturedImage FromBitmap(SKBitmap bitmap, DateTimeOffset capturedAtUtc)
    {
        return new CapturedImage(bitmap, capturedAtUtc);
    }

    internal SKBitmap AsSkBitmap()
    {
        lock (_sync)
        {
            return _skBitmap ??= CreateSkBitmap(_pngBytes ?? throw new InvalidOperationException("No image payload available."));
        }
    }

    private static Bitmap CreateBitmap(SKBitmap bitmap)
    {
        return new Bitmap(
            PixelFormat.Bgra8888,
            AlphaFormat.Premul,
            bitmap.GetPixels(),
            new PixelSize(bitmap.Width, bitmap.Height),
            new Vector(96, 96),
            bitmap.RowBytes);
    }

    private static SKBitmap CreateSkBitmap(byte[] pngBytes)
    {
        return SKBitmap.Decode(pngBytes)
            ?? throw new InvalidOperationException("Invalid PNG payload.");
    }

    private static byte[] CreatePngBytes(SKBitmap bitmap)
    {
        using var image = SKImage.FromBitmap(bitmap);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return data?.ToArray() ?? throw new InvalidOperationException("Failed to encode PNG payload.");
    }
}
