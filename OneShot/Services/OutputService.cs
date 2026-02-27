using System.Drawing;
using System.Globalization;
using Avalonia.Input;
using Avalonia.Platform.Storage;
using OneShot.Models;

namespace OneShot.Services;

public sealed class OutputService
{
    private readonly string _saveDir;
    private readonly string _tempDir;

    public OutputService(string? saveDir = null, string? tempDir = null)
    {
        _saveDir = string.IsNullOrWhiteSpace(saveDir)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures), "Screenshots")
            : saveDir;
        _tempDir = string.IsNullOrWhiteSpace(tempDir)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OneShot", "Temp")
            : tempDir;
        Directory.CreateDirectory(_saveDir);
        Directory.CreateDirectory(_tempDir);
        CleanupTempFiles();
    }

    public async Task<string> SaveAsync(CapturedImage image)
    {
        try
        {
            string fileName = $"OneShot_{DateTime.Now.ToString("yyyyMMdd_HHmmss_fff", CultureInfo.InvariantCulture)}.png";
            string path = Path.Combine(_saveDir, fileName);
            await File.WriteAllBytesAsync(path, image.PngBytes);
            return path;
        }
        catch (Exception ex)
        {
            throw new IOException($"Failed to save screenshot to '{_saveDir}'.", ex);
        }
    }

    public Task CopyToClipboardAsync(CapturedImage image)
    {
        try
        {
            using var stream = new MemoryStream(image.PngBytes, writable: false);
            using var drawingImage = Image.FromStream(stream);
            System.Windows.Forms.Clipboard.SetImage(drawingImage);
        }
        catch (Exception ex)
        {
            throw new InvalidOperationException("Failed to copy image to clipboard.", ex);
        }

        return Task.CompletedTask;
    }

    public string WriteTempDragFile(CapturedImage image)
    {
        var tempPath = Path.Combine(_tempDir, $"drag_{Guid.NewGuid():N}.png");
        try
        {
            File.WriteAllBytes(tempPath, image.PngBytes);
        }
        catch (Exception ex)
        {
            throw new IOException($"Failed to create drag temp file '{tempPath}'.", ex);
        }

        return tempPath;
    }

    public async Task<IDataObject> BuildDragDataAsync(CapturedImage image, IStorageProvider? storageProvider = null, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var tempPath = WriteTempDragFile(image);
        var data = new DataObject();

        // Text helps terminal drops; FileDrop keeps Explorer-compatible file semantics.
        data.Set(DataFormats.Text, tempPath);
        data.Set(DataFormats.FileNames, new[] { tempPath });
        data.Set("FileDrop", new[] { tempPath });
        if (storageProvider is not null)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var storageFile = await storageProvider.TryGetFileFromPathAsync(new Uri(tempPath));
            if (storageFile is not null)
            {
                data.Set(DataFormats.Files, new[] { storageFile });
            }
        }

        return data;
    }

    private void CleanupTempFiles()
    {
        foreach (var file in Directory.EnumerateFiles(_tempDir, "drag_*.png"))
        {
            try
            {
                File.Delete(file);
            }
            catch
            {
            }
        }
    }
}
