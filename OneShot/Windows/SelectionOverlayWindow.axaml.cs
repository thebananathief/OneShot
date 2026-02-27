using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using OneShot.Models;
using System.Runtime.InteropServices;

namespace OneShot.Windows;

public partial class SelectionOverlayWindow : Window
{
    private readonly TaskCompletionSource<Rect?> _tcs = new();
    private readonly System.Drawing.Rectangle _virtualScreen;
    private readonly NativeMethods.LowLevelKeyboardProc _keyboardProc;
    private nint _keyboardHook;
    private Point? _start;
    private NativeMethods.NativePoint? _startCursorPosition;

    public SelectionOverlayWindow(CapturedImage virtualScreenCapture, System.Drawing.Rectangle virtualScreen)
    {
        InitializeComponent();
        _keyboardProc = OnLowLevelKeyboard;

        _virtualScreen = virtualScreen;
        BackgroundImage.Source = virtualScreenCapture.Bitmap;
        ApplyVirtualScreenBounds();

        PointerPressed += OnPointerPressed;
        PointerMoved += OnPointerMoved;
        PointerReleased += OnPointerReleased;
        KeyDown += OnKeyDown;
        Opened += (_, _) =>
        {
            ApplyVirtualScreenBounds();
            InstallEscapeHook();
        };
        Closed += (_, _) =>
        {
            RemoveEscapeHook();
            if (!_tcs.Task.IsCompleted)
            {
                _tcs.TrySetResult(null);
            }
        };
    }

    public Task<Rect?> GetSelectionAsync()
    {
        Show();
        return _tcs.Task;
    }

    private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var point = e.GetCurrentPoint(this);
        if (point.Properties.IsRightButtonPressed)
        {
            _start = null;
            _startCursorPosition = null;
            _tcs.TrySetResult(null);
            Close();
            return;
        }

        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        _start = e.GetPosition(this);
        if (NativeMethods.TryGetCursorPos(out var cursorPosition))
        {
            _startCursorPosition = cursorPosition;
        }

        SelectionRect.IsVisible = true;
        UpdateRect(_start.Value, _start.Value);
        e.Pointer.Capture(this);
    }

    private void OnPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_start is null)
        {
            return;
        }

        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            return;
        }

        UpdateRect(_start.Value, e.GetPosition(this));
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_start is null)
        {
            return;
        }

        if (e.InitialPressMouseButton != MouseButton.Left)
        {
            return;
        }

        e.Pointer.Capture(null);

        Point end = e.GetPosition(this);
        Rect localRect = BuildRect(_start.Value, end);
        _start = null;

        if (localRect.Width < 3 || localRect.Height < 3)
        {
            _startCursorPosition = null;
            _tcs.TrySetResult(null);
            Close();
            return;
        }

        Rect absolute;
        if (_startCursorPosition.HasValue && NativeMethods.TryGetCursorPos(out var endCursorPosition))
        {
            var startCursorPosition = _startCursorPosition.Value;
            absolute = new Rect(
                Math.Min(startCursorPosition.X, endCursorPosition.X),
                Math.Min(startCursorPosition.Y, endCursorPosition.Y),
                Math.Abs(endCursorPosition.X - startCursorPosition.X),
                Math.Abs(endCursorPosition.Y - startCursorPosition.Y));
        }
        else
        {
            // Fallback for environments where cursor lookup fails.
            PixelPoint topLeftPx = ClientDipToScreenPixel(localRect.TopLeft);
            PixelPoint bottomRightPx = ClientDipToScreenPixel(localRect.BottomRight);
            absolute = new Rect(
                Math.Min(topLeftPx.X, bottomRightPx.X),
                Math.Min(topLeftPx.Y, bottomRightPx.Y),
                Math.Abs(bottomRightPx.X - topLeftPx.X),
                Math.Abs(bottomRightPx.Y - topLeftPx.Y));
        }

        _startCursorPosition = null;
        _tcs.TrySetResult(absolute);
        Close();
    }

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            _start = null;
            _startCursorPosition = null;
            _tcs.TrySetResult(null);
            Close();
        }
    }

    private void UpdateRect(Point start, Point end)
    {
        Rect rect = BuildRect(start, end);
        Canvas.SetLeft(SelectionRect, rect.X);
        Canvas.SetTop(SelectionRect, rect.Y);
        SelectionRect.Width = rect.Width;
        SelectionRect.Height = rect.Height;
    }

    private static Rect BuildRect(Point a, Point b)
    {
        return new Rect(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Abs(a.X - b.X), Math.Abs(a.Y - b.Y));
    }

    private void ApplyVirtualScreenBounds()
    {
        double scaling = RenderScaling;
        if (scaling <= 0)
        {
            scaling = 1.0;
        }

        Position = new PixelPoint(_virtualScreen.Left, _virtualScreen.Top);
        Width = _virtualScreen.Width / scaling;
        Height = _virtualScreen.Height / scaling;
    }

    private PixelPoint ClientDipToScreenPixel(Point dipPoint)
    {
        double scaling = RenderScaling;
        if (scaling <= 0)
        {
            scaling = 1.0;
        }

        int clientX = (int)Math.Round(dipPoint.X * scaling);
        int clientY = (int)Math.Round(dipPoint.Y * scaling);
        return new PixelPoint(Position.X + clientX, Position.Y + clientY);
    }

    private void InstallEscapeHook()
    {
        if (_keyboardHook != nint.Zero)
        {
            return;
        }

        _keyboardHook = NativeMethods.SetWindowsHookEx(NativeMethods.WhKeyboardLl, _keyboardProc, nint.Zero, 0);
    }

    private void RemoveEscapeHook()
    {
        if (_keyboardHook == nint.Zero)
        {
            return;
        }

        NativeMethods.UnhookWindowsHookEx(_keyboardHook);
        _keyboardHook = nint.Zero;
    }

    private nint OnLowLevelKeyboard(int code, nint wParam, nint lParam)
    {
        if (code >= 0 && wParam == NativeMethods.WmKeyDown)
        {
            var key = Marshal.PtrToStructure<NativeMethods.Kbdllhookstruct>(lParam);
            if (key.vkCode == NativeMethods.VkEscape)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    if (!_tcs.Task.IsCompleted)
                    {
                        _tcs.TrySetResult(null);
                    }

                    if (IsVisible)
                    {
                        Close();
                    }
                });

                return (nint)1;
            }
        }

        return NativeMethods.CallNextHookEx(nint.Zero, code, wParam, lParam);
    }

    private static class NativeMethods
    {
        internal const int WhKeyboardLl = 13;
        internal static readonly nint WmKeyDown = (nint)0x0100;
        internal const uint VkEscape = 0x1B;

        internal delegate nint LowLevelKeyboardProc(int nCode, nint wParam, nint lParam);

        [StructLayout(LayoutKind.Sequential)]
        internal struct Kbdllhookstruct
        {
            public uint vkCode;
            public uint scanCode;
            public uint flags;
            public uint time;
            public nint dwExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativePoint
        {
            public int X;
            public int Y;
        }

        [DllImport("user32.dll")]
        internal static extern nint SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, nint hMod, uint dwThreadId);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool UnhookWindowsHookEx(nint hhk);

        [DllImport("user32.dll")]
        internal static extern nint CallNextHookEx(nint hhk, int nCode, nint wParam, nint lParam);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetCursorPos(out NativePoint point);

        internal static bool TryGetCursorPos(out NativePoint point)
        {
            return GetCursorPos(out point);
        }
    }
}
