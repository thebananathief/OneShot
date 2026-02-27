namespace OneShot.Services;

public interface IScreenCaptureBackend
{
    byte[] CapturePng(int x, int y, int width, int height);
}
