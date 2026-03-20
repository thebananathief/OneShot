using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using OneShot.Models;
using OneShot.Services;
using System.Runtime.InteropServices;

namespace OneShot.Windows;

public partial class SelectionOverlayWindow : Window, ISelectionOverlaySurface
{
    private System.Drawing.Rectangle _monitorBounds;
    private bool _revealOnOpened;
    private bool _isPrewarmOpen;

    public SelectionOverlayWindow()
    {
        InitializeComponent();
        _monitorBounds = new System.Drawing.Rectangle(0, 0, 1, 1);
        Opacity = 1;

        PointerPressed += OnPointerPressed;
        Opened += (_, _) =>
        {
            ApplyMonitorBoundsAndAlignClientOrigin();

            if (_isPrewarmOpen)
            {
                _isPrewarmOpen = false;
                SurfaceOpened?.Invoke(this, EventArgs.Empty);
                return;
            }

            if (_revealOnOpened)
            {
                Dispatcher.UIThread.Post(
                    () =>
                    {
                        if (!_revealOnOpened || _isPrewarmOpen)
                        {
                            return;
                        }

                        ApplyMonitorBoundsAndAlignClientOrigin();
                        Opacity = 1;
                        _revealOnOpened = false;
                        SurfaceOpened?.Invoke(this, EventArgs.Empty);
                    },
                    DispatcherPriority.Render);
                return;
            }

            SurfaceOpened?.Invoke(this, EventArgs.Empty);
        };
        ScalingChanged += (_, _) => ApplyMonitorBoundsAndAlignClientOrigin();
        Closed += (_, _) => SurfaceClosed?.Invoke(this, EventArgs.Empty);
    }

    public event EventHandler<PixelPoint>? DragStarted;

    public event EventHandler? CancelRequested;

    public event EventHandler? SurfaceClosed;

    public event EventHandler? SurfaceOpened;

    public bool IsPooledReusable => true;

    public void PrepareForSnapshot(CapturedImage monitorCapture, System.Drawing.Rectangle monitorBounds)
    {
        Opacity = 0;
        _revealOnOpened = true;
        _isPrewarmOpen = false;
        _monitorBounds = monitorBounds;
        ClearSelectionVisual();
        BackgroundImage.Source = monitorCapture.Bitmap;
        ApplyMonitorBoundsAndAlignClientOrigin();
    }

    public void PrepareForPrewarm(System.Drawing.Rectangle monitorBounds)
    {
        Opacity = 0;
        _revealOnOpened = false;
        _isPrewarmOpen = true;
        _monitorBounds = monitorBounds;
        ClearSelectionVisual();
        BackgroundImage.Source = null;
        ApplyMonitorBoundsAndAlignClientOrigin();
    }

    public void ResetForPooling()
    {
        ClearSelectionVisual();
    }

    private void ClearSelectionVisual()
    {
        SelectionRect.IsVisible = false;
        Canvas.SetLeft(SelectionRect, 0);
        Canvas.SetTop(SelectionRect, 0);
        SelectionRect.Width = 0;
        SelectionRect.Height = 0;
    }

    public void HideForPooling()
    {
        _revealOnOpened = false;
        _isPrewarmOpen = false;
        Opacity = 0;
        ResetForPooling();
        Hide();
    }

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
            ClearSelectionVisual();
            return;
        }

        var intersected = SelectionGeometry.IntersectWithMonitor(selectionInScreenPixels.Value, _monitorBounds);
        if (!intersected.HasValue)
        {
            ClearSelectionVisual();
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
