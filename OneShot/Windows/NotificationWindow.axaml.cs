using Avalonia.Animation;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Styling;
using System.Diagnostics;
using System.Runtime.InteropServices;
using OneShot.Models;
using OneShot.Services;

namespace OneShot.Windows;

public partial class NotificationWindow : Window
{
    private readonly OutputService _output;
    private readonly SolidColorBrush _cardBackgroundBrush;
    private readonly Animation _updatePulseAnimation;
    private readonly Color _baseBackgroundColor;
    private Point _dragStart;
    private bool _dragInProgress;
    private CancellationTokenSource? _updatePulseCts;

    public event EventHandler? MarkupRequested;
    public event EventHandler? DismissRequested;

    public CapturedImage CurrentImage { get; private set; }

    public NotificationWindow(CapturedImage image, OutputService output)
    {
        InitializeComponent();
        CurrentImage = image;
        _output = output;
        _baseBackgroundColor = (CardBorder.Background as ISolidColorBrush)?.Color ?? Color.Parse("#FF2D2D30");
        _cardBackgroundBrush = new SolidColorBrush(_baseBackgroundColor);
        CardBorder.Background = _cardBackgroundBrush;
        _updatePulseAnimation = CreateUpdatePulseAnimation();

        Thumbnail.Source = image.Bitmap;

        MarkupButton.Click += (_, _) => MarkupRequested?.Invoke(this, EventArgs.Empty);
        DismissButton.Click += (_, _) => DismissRequested?.Invoke(this, EventArgs.Empty);

        ThumbBorder.PointerPressed += OnThumbPointerPressed;
        ThumbBorder.PointerMoved += OnThumbPointerMoved;
        Closed += (_, _) => CancelUpdatePulse();
    }

    public void UpdateImage(CapturedImage image)
    {
        CurrentImage = image;
        Thumbnail.Source = image.Bitmap;
        _ = RunUpdatePulseAsync();
    }

    private Animation CreateUpdatePulseAnimation()
    {
        Color pulseColor = Color.Parse("#FF3D79D8");

        var animation = new Animation
        {
            Duration = TimeSpan.FromMilliseconds(500),
            FillMode = FillMode.None
        };

        animation.Children.Add(new KeyFrame
        {
            Cue = new Cue(0d),
            Setters =
            {
                new Setter(SolidColorBrush.ColorProperty, _baseBackgroundColor)
            }
        });

        animation.Children.Add(new KeyFrame
        {
            Cue = new Cue(0.45d),
            Setters =
            {
                new Setter(SolidColorBrush.ColorProperty, pulseColor)
            }
        });

        animation.Children.Add(new KeyFrame
        {
            Cue = new Cue(1d),
            Setters =
            {
                new Setter(SolidColorBrush.ColorProperty, _baseBackgroundColor)
            }
        });

        return animation;
    }

    private async Task RunUpdatePulseAsync()
    {
        var previous = Interlocked.Exchange(ref _updatePulseCts, new CancellationTokenSource());
        previous?.Cancel();
        previous?.Dispose();

        var cts = _updatePulseCts;
        if (cts is null || !IsVisible)
        {
            _cardBackgroundBrush.Color = _baseBackgroundColor;
            return;
        }

        _cardBackgroundBrush.Color = _baseBackgroundColor;

        try
        {
            await _updatePulseAnimation.RunAsync(_cardBackgroundBrush, cts.Token);
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            _cardBackgroundBrush.Color = _baseBackgroundColor;
            if (ReferenceEquals(_updatePulseCts, cts))
            {
                _updatePulseCts = null;
            }

            cts.Dispose();
        }
    }

    private void CancelUpdatePulse()
    {
        var cts = Interlocked.Exchange(ref _updatePulseCts, null);
        cts?.Cancel();
        cts?.Dispose();
        _cardBackgroundBrush.Color = _baseBackgroundColor;
    }

    private void OnThumbPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!e.GetCurrentPoint(ThumbBorder).Properties.IsLeftButtonPressed)
        {
            return;
        }

        _dragStart = e.GetPosition(ThumbBorder);
    }

    private async void OnThumbPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_dragInProgress)
        {
            return;
        }

        if (!e.GetCurrentPoint(ThumbBorder).Properties.IsLeftButtonPressed)
        {
            return;
        }

        var current = e.GetPosition(ThumbBorder);
        if (Math.Abs(current.X - _dragStart.X) < 4 && Math.Abs(current.Y - _dragStart.Y) < 4)
        {
            return;
        }

        _dragInProgress = true;
        try
        {
            var topLevel = GetTopLevel(this);
            var data = await _output.BuildDragDataAsync(CurrentImage, topLevel?.StorageProvider);
            await DragDrop.DoDragDrop(e, data, DragDropEffects.Copy);
            FocusDropTargetUnderCursor();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to start drag operation: {ex}");
        }
        finally
        {
            _dragInProgress = false;
            if (!e.GetCurrentPoint(ThumbBorder).Properties.IsLeftButtonPressed)
            {
                _dragStart = default;
            }
        }
    }

    private static void FocusDropTargetUnderCursor()
    {
        if (!NativeMethods.GetCursorPos(out var point))
        {
            return;
        }

        nint targetWindow = NativeMethods.WindowFromPoint(point);
        if (targetWindow == nint.Zero)
        {
            return;
        }

        NativeMethods.SetForegroundWindow(targetWindow);
    }

    private static class NativeMethods
    {
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool GetCursorPos(out POINT lpPoint);

        [DllImport("user32.dll")]
        internal static extern nint WindowFromPoint(POINT point);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetForegroundWindow(nint hWnd);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }
}
