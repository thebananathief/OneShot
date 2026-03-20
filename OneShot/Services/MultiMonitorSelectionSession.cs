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

    private readonly IReadOnlyList<MonitorSnapshot> _monitorSnapshots;
    private readonly Func<MonitorSnapshot, ValueTask<PooledOverlayLease>> _overlayLeaseFactory;
    private readonly TaskCompletionSource<Rect?> _selectionTcs = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private readonly List<ISelectionOverlaySurface> _surfaces = new();
    private readonly List<PooledOverlayLease> _leases = new();
    private readonly NativeMethods.LowLevelKeyboardProc _keyboardProc;
    private readonly NativeMethods.LowLevelMouseProc _mouseProc;
    private readonly Action<string>? _log;
    private readonly string _invocationId;
    private readonly Action<string, string, long, object?>? _trace;
    private readonly bool _installSystemHooks;
    private readonly Stopwatch _stopwatch = Stopwatch.StartNew();

    private nint _keyboardHook;
    private nint _mouseHook;
    private PixelPoint? _dragStart;
    private PixelPoint? _dragCurrent;
    private bool _started;
    private bool _isFinishing;
    private bool _isDragging;
    private int _openedSurfaceCount;

    public MultiMonitorSelectionSession(
        IReadOnlyList<MonitorSnapshot> monitorSnapshots,
        Func<MonitorSnapshot, ValueTask<PooledOverlayLease>>? overlayLeaseFactory = null,
        Action<string>? log = null,
        string invocationId = "",
        Action<string, string, long, object?>? trace = null,
        bool installSystemHooks = true)
    {
        _monitorSnapshots = monitorSnapshots;
        _overlayLeaseFactory = overlayLeaseFactory ?? (snapshot => ValueTask.FromResult(CreateFreshLease(snapshot)));
        _keyboardProc = OnLowLevelKeyboard;
        _mouseProc = OnLowLevelMouse;
        _log = log;
        _invocationId = invocationId;
        _trace = trace;
        _installSystemHooks = installSystemHooks;
    }

    public async Task<Rect?> GetSelectionAsync()
    {
        if (_started)
        {
            throw new InvalidOperationException("Selection session has already started.");
        }

        _started = true;
        if (_monitorSnapshots.Count == 0)
        {
            _selectionTcs.TrySetResult(null);
            return await _selectionTcs.Task;
        }

        foreach (var snapshot in _monitorSnapshots)
        {
            Trace("overlay_surface_create_start", new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height });
            var lease = await _overlayLeaseFactory(snapshot);
            var surface = lease.Surface;
            surface.DragStarted += OnDragStarted;
            surface.CancelRequested += OnCancelRequested;
            surface.SurfaceClosed += OnSurfaceClosed;
            surface.SurfaceOpened += OnSurfaceOpened;
            _leases.Add(lease);
            _surfaces.Add(surface);
            Trace("overlay_surface_create_complete", new { snapshot.Bounds.Left, snapshot.Bounds.Top, snapshot.Bounds.Width, snapshot.Bounds.Height });
        }

        if (_installSystemHooks)
        {
            InstallEscapeHook();
            InstallMouseHook();
        }

        foreach (var surface in _surfaces)
        {
            Trace("overlay_show_start", new { SurfaceIndex = _surfaces.IndexOf(surface) });
            surface.Show();
        }

        return await _selectionTcs.Task;
    }

    private void OnSurfaceOpened(object? sender, EventArgs e)
    {
        _openedSurfaceCount++;
        Trace("overlay_surface_opened", new { OpenedCount = _openedSurfaceCount, Total = _surfaces.Count });
        if (_openedSurfaceCount == _surfaces.Count)
        {
            Trace("all_overlays_opened", new { Total = _surfaces.Count });
        }
    }

    private void OnDragStarted(object? sender, PixelPoint cursorPosition)
    {
        if (_isFinishing)
        {
            return;
        }

        _dragStart = cursorPosition;
        _dragCurrent = cursorPosition;
        _isDragging = true;
        UpdateSelectionVisuals();
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
        _isDragging = false;
        _dragStart = null;
        _dragCurrent = null;
        RemoveEscapeHook();
        RemoveMouseHook();

        foreach (var surface in _surfaces.ToList())
        {
            surface.DragStarted -= OnDragStarted;
            surface.CancelRequested -= OnCancelRequested;
            surface.SurfaceClosed -= OnSurfaceClosed;
            surface.SurfaceOpened -= OnSurfaceOpened;
        }

        foreach (var lease in _leases.ToList())
        {
            lease.Dispose();
        }

        _surfaces.Clear();
        _leases.Clear();
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

    private void InstallMouseHook()
    {
        if (_mouseHook != nint.Zero)
        {
            return;
        }

        _mouseHook = NativeMethods.SetWindowsHookExMouse(NativeMethods.WhMouseLl, _mouseProc, nint.Zero, 0);
    }

    private void RemoveMouseHook()
    {
        if (_mouseHook == nint.Zero)
        {
            return;
        }

        NativeMethods.UnhookWindowsHookEx(_mouseHook);
        _mouseHook = nint.Zero;
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

    private nint OnLowLevelMouse(int code, nint wParam, nint lParam)
    {
        if (code >= 0)
        {
            var mouse = Marshal.PtrToStructure<NativeMethods.Msllhookstruct>(lParam);
            var point = new PixelPoint(mouse.pt.X, mouse.pt.Y);

            if (wParam == NativeMethods.WmMouseMove && _isDragging)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    if (_isFinishing || !_isDragging)
                    {
                        return;
                    }

                    _dragCurrent = point;
                    UpdateSelectionVisuals();
                });
            }
            else if (wParam == NativeMethods.WmLButtonUp && _isDragging)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    if (_isFinishing || !_isDragging)
                    {
                        return;
                    }

                    _dragCurrent = point;
                    _isDragging = false;
                    CompleteSelection();
                });
            }
            else if (wParam == NativeMethods.WmRButtonDown)
            {
                Dispatcher.UIThread.Post(() =>
                {
                    if (!_isFinishing)
                    {
                        Finish(null);
                    }
                });
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

    private void Trace(string phase, object? details = null)
    {
        if (string.IsNullOrWhiteSpace(_invocationId))
        {
            return;
        }

        _trace?.Invoke(_invocationId, phase, _stopwatch.ElapsedMilliseconds, details);
    }

    private static PooledOverlayLease CreateFreshLease(MonitorSnapshot snapshot)
    {
        var surface = new SelectionOverlayWindow();
        surface.PrepareForSnapshot(snapshot.Image, snapshot.Bounds);
        return new PooledOverlayLease(surface, releasedSurface => releasedSurface.Close());
    }

    private static class NativeMethods
    {
        internal const int WhKeyboardLl = 13;
        internal const int WhMouseLl = 14;
        internal static readonly nint WmKeyDown = (nint)0x0100;
        internal static readonly nint WmMouseMove = (nint)0x0200;
        internal static readonly nint WmLButtonUp = (nint)0x0202;
        internal static readonly nint WmRButtonDown = (nint)0x0204;
        internal const uint VkEscape = 0x1B;

        internal delegate nint LowLevelKeyboardProc(int nCode, nint wParam, nint lParam);
        internal delegate nint LowLevelMouseProc(int nCode, nint wParam, nint lParam);

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
        internal struct Msllhookstruct
        {
            public NativePoint pt;
            public uint mouseData;
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

        [DllImport("user32.dll", EntryPoint = "SetWindowsHookEx")]
        internal static extern nint SetWindowsHookExMouse(int idHook, LowLevelMouseProc lpfn, nint hMod, uint dwThreadId);

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
