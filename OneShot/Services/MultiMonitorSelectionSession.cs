using System.Diagnostics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;
using OneShot.Models;
using OneShot.Windows;

namespace OneShot.Services;

internal sealed class MultiMonitorSelectionSession
{
    private const int MinimumSelectionWidth = 3;
    private const int MinimumSelectionHeight = 3;
    private const int CursorPollIntervalMs = 8;

    private readonly IReadOnlyList<MonitorSnapshot> _monitorSnapshots;
    private readonly Func<MonitorSnapshot, ISelectionOverlaySurface> _surfaceFactory;
    private readonly TaskCompletionSource<Rect?> _selectionTcs = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly List<ISelectionOverlaySurface> _surfaces = new();
    private readonly NativeMethods.LowLevelKeyboardProc _keyboardProc;
    private readonly Action<string>? _log;

    private nint _keyboardHook;
    private PixelPoint? _dragStart;
    private PixelPoint? _dragCurrent;
    private CancellationTokenSource? _dragPollingCts;
    private bool _started;
    private bool _isFinishing;

    public MultiMonitorSelectionSession(IReadOnlyList<MonitorSnapshot> monitorSnapshots, Func<MonitorSnapshot, ISelectionOverlaySurface>? surfaceFactory = null, Action<string>? log = null)
    {
        _monitorSnapshots = monitorSnapshots;
        _surfaceFactory = surfaceFactory ?? (snapshot => new SelectionOverlayWindow(snapshot.Image, snapshot.Bounds));
        _keyboardProc = OnLowLevelKeyboard;
        _log = log;
    }

    public Task<Rect?> GetSelectionAsync()
    {
        if (_started)
        {
            throw new InvalidOperationException("Selection session has already started.");
        }

        _started = true;
        if (_monitorSnapshots.Count == 0)
        {
            _selectionTcs.TrySetResult(null);
            return _selectionTcs.Task;
        }

        foreach (var snapshot in _monitorSnapshots)
        {
            var surface = _surfaceFactory(snapshot);
            surface.DragStarted += OnDragStarted;
            surface.CancelRequested += OnCancelRequested;
            surface.SurfaceClosed += OnSurfaceClosed;
            _surfaces.Add(surface);
        }

        InstallEscapeHook();
        foreach (var surface in _surfaces)
        {
            surface.Show();
        }

        return _selectionTcs.Task;
    }

    private void OnDragStarted(object? sender, PixelPoint cursorPosition)
    {
        if (_isFinishing)
        {
            return;
        }

        _dragStart = cursorPosition;
        _dragCurrent = cursorPosition;
        UpdateSelectionVisuals();
        StartDragPolling();
    }

    private void OnCancelRequested(object? sender, EventArgs e)
    {
        Finish(null);
    }

    private void OnSurfaceClosed(object? sender, EventArgs e)
    {
        if (_isFinishing)
        {
            return;
        }

        Finish(null);
    }

    private void StartDragPolling()
    {
        _dragPollingCts?.Cancel();
        _dragPollingCts?.Dispose();
        _dragPollingCts = new CancellationTokenSource();
        _ = PollDragLoopAsync(_dragPollingCts.Token);
    }

    private async Task PollDragLoopAsync(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                if (NativeMethods.TryGetCursorPos(out var cursorPosition))
                {
                    _dragCurrent = new PixelPoint(cursorPosition.X, cursorPosition.Y);
                    UpdateSelectionVisuals();
                }

                if (!NativeMethods.IsLeftButtonPressed())
                {
                    CompleteSelection();
                    return;
                }

                await Task.Delay(CursorPollIntervalMs, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            Log($"Selection drag polling failed: {ex}");
            Finish(null);
        }
    }

    private void CompleteSelection()
    {
        if (!_dragStart.HasValue || !_dragCurrent.HasValue)
        {
            Finish(null);
            return;
        }

        Rect selectedRect = SelectionGeometry.BuildRect(_dragStart.Value, _dragCurrent.Value);
        if (!SelectionGeometry.MeetsMinimumSize(selectedRect, MinimumSelectionWidth, MinimumSelectionHeight))
        {
            Finish(null);
            return;
        }

        Finish(selectedRect);
    }

    private void UpdateSelectionVisuals()
    {
        Rect? selection = null;
        if (_dragStart.HasValue && _dragCurrent.HasValue)
        {
            selection = SelectionGeometry.BuildRect(_dragStart.Value, _dragCurrent.Value);
        }

        foreach (var surface in _surfaces)
        {
            if (surface.IsVisible)
            {
                surface.SetSelection(selection);
            }
        }
    }

    private void Finish(Rect? selection)
    {
        if (_isFinishing)
        {
            return;
        }

        _isFinishing = true;
        _dragStart = null;
        _dragCurrent = null;
        _dragPollingCts?.Cancel();
        _dragPollingCts?.Dispose();
        _dragPollingCts = null;
        RemoveEscapeHook();

        foreach (var surface in _surfaces.ToList())
        {
            surface.DragStarted -= OnDragStarted;
            surface.CancelRequested -= OnCancelRequested;
            surface.SurfaceClosed -= OnSurfaceClosed;
            if (surface.IsVisible)
            {
                surface.Close();
            }
        }

        _selectionTcs.TrySetResult(selection);
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
                Dispatcher.UIThread.Post(() => Finish(null));
                return (nint)1;
            }
        }

        return NativeMethods.CallNextHookEx(nint.Zero, code, wParam, lParam);
    }

    private void Log(string message)
    {
        if (_log is not null)
        {
            _log(message);
            return;
        }

        Debug.WriteLine(message);
    }

    private static class NativeMethods
    {
        internal const int WhKeyboardLl = 13;
        internal static readonly nint WmKeyDown = (nint)0x0100;
        internal const uint VkEscape = 0x1B;
        private const int VkLButton = 0x01;

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

        [DllImport("user32.dll")]
        private static extern short GetAsyncKeyState(int vKey);

        internal static bool TryGetCursorPos(out NativePoint point)
        {
            return GetCursorPos(out point);
        }

        internal static bool IsLeftButtonPressed()
        {
            return (GetAsyncKeyState(VkLButton) & 0x8000) != 0;
        }
    }
}
