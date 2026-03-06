using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using OneShot.Models;
using OneShot.Services;
using System.Runtime.InteropServices;

namespace OneShot.Windows;

public partial class SelectionOverlayWindow : Window, ISelectionOverlaySurface
{
    private readonly System.Drawing.Rectangle _monitorBounds;

    public SelectionOverlayWindow(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds)
    {
        InitializeComponent();
        _monitorBounds = monitorBounds;
        BackgroundImage.Source = monitorCapture.Bitmap;
        ApplyMonitorBoundsAndAlignClientOrigin();

        PointerPressed += OnPointerPressed;
        Opened += (_, _) => ApplyMonitorBoundsAndAlignClientOrigin();
        ScalingChanged += (_, _) => ApplyMonitorBoundsAndAlignClientOrigin();
        Closed += (_, _) => SurfaceClosed?.Invoke(this, EventArgs.Empty);
    }

    public event EventHandler<PixelPoint>? DragStarted;

    public event EventHandler? CancelRequested;

    public event EventHandler? SurfaceClosed;

    private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var point = e.GetCurrentPoint(this);
        if (point.Properties.IsRightButtonPressed)
        {
            CancelRequested?.Invoke(this, EventArgs.Empty);
            return;
        }

        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (NativeMethods.TryGetCursorPos(out var cursorPosition))
        {
            DragStarted?.Invoke(this, new PixelPoint(cursorPosition.X, cursorPosition.Y));
            return;
        }

        DragStarted?.Invoke(this, ClientDipToScreenPixel(e.GetPosition(this)));
    }

    public void SetSelection(Rect? selectionInScreenPixels)
    {
        if (!selectionInScreenPixels.HasValue)
        {
            SelectionRect.IsVisible = false;
            return;
        }

        var intersected = SelectionGeometry.IntersectWithMonitor(selectionInScreenPixels.Value, _monitorBounds);
        if (!intersected.HasValue)
        {
            SelectionRect.IsVisible = false;
            return;
        }

        Rect selection = intersected.Value;
        double scaling = GetRenderScaling();
        double x = (selection.X - _monitorBounds.Left) / scaling;
        double y = (selection.Y - _monitorBounds.Top) / scaling;
        double width = selection.Width / scaling;
        double height = selection.Height / scaling;
        SelectionRect.IsVisible = true;
        Canvas.SetLeft(SelectionRect, x);
        Canvas.SetTop(SelectionRect, y);
        SelectionRect.Width = width;
        SelectionRect.Height = height;
    }

    private PixelPoint ClientDipToScreenPixel(Point dipPoint)
    {
        double scaling = GetRenderScaling();
        int clientX = (int)Math.Round(dipPoint.X * scaling);
        int clientY = (int)Math.Round(dipPoint.Y * scaling);
        if (TryGetClientScreenOrigin(out var clientOrigin))
        {
            return new PixelPoint(clientOrigin.X + clientX, clientOrigin.Y + clientY);
        }

        return new PixelPoint(Position.X + clientX, Position.Y + clientY);
    }

    private void ApplyMonitorBoundsAndAlignClientOrigin()
    {
        double scaling = GetRenderScaling();
        Position = new PixelPoint(_monitorBounds.Left, _monitorBounds.Top);
        Width = _monitorBounds.Width / scaling;
        Height = _monitorBounds.Height / scaling;

        if (!TryGetClientScreenOrigin(out var clientOrigin))
        {
            return;
        }

        int dx = clientOrigin.X - _monitorBounds.Left;
        int dy = clientOrigin.Y - _monitorBounds.Top;
        if (dx == 0 && dy == 0)
        {
            return;
        }

        Position = new PixelPoint(Position.X - dx, Position.Y - dy);
    }

    private bool TryGetClientScreenOrigin(out NativeMethods.NativePoint origin)
    {
        origin = default;
        var handle = TryGetPlatformHandle();
        if (handle is null || handle.Handle == nint.Zero)
        {
            return false;
        }

        return NativeMethods.ClientToScreen(handle.Handle, ref origin);
    }

    private double GetRenderScaling()
    {
        double scaling = RenderScaling;
        return scaling > 0 ? scaling : 1.0;
    }

    private static class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        internal struct NativePoint
        {
            public int X;
            public int Y;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetCursorPos(out NativePoint point);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool ClientToScreen(nint hWnd, ref NativePoint point);

        internal static bool TryGetCursorPos(out NativePoint point)
        {
            return GetCursorPos(out point);
        }
    }
}
