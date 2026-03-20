using System.Runtime.InteropServices;

namespace OneShot.Services;

internal static class MonitorTopologyService
{
    public static IReadOnlyList<System.Drawing.Rectangle> GetMonitorBounds()
    {
        nint previousDpiContext = NativeMethods.SetThreadDpiAwarenessContext(NativeMethods.DpiAwarenessContextPerMonitorAwareV2);
        try
        {
            var bounds = new List<System.Drawing.Rectangle>();
            bool success = NativeMethods.EnumDisplayMonitors(
                nint.Zero,
                nint.Zero,
                (hMonitor, _, _, _) =>
                {
                    if (NativeMethods.TryGetMonitorBounds(hMonitor, out var monitorBounds))
                    {
                        bounds.Add(monitorBounds);
                    }

                    return true;
                },
                nint.Zero);

            if (success && bounds.Count > 0)
            {
                return bounds;
            }

            var fallbackLeft = NativeMethods.GetSystemMetrics(NativeMethods.SmXvirtualscreen);
            var fallbackTop = NativeMethods.GetSystemMetrics(NativeMethods.SmYvirtualscreen);
            var fallbackWidth = NativeMethods.GetSystemMetrics(NativeMethods.SmCxvirtualscreen);
            var fallbackHeight = NativeMethods.GetSystemMetrics(NativeMethods.SmCyvirtualscreen);
            return new[] { new System.Drawing.Rectangle(fallbackLeft, fallbackTop, fallbackWidth, fallbackHeight) };
        }
        finally
        {
            if (previousDpiContext != nint.Zero)
            {
                NativeMethods.SetThreadDpiAwarenessContext(previousDpiContext);
            }
        }
    }

    private static class NativeMethods
    {
        internal const int SmXvirtualscreen = 76;
        internal const int SmYvirtualscreen = 77;
        internal const int SmCxvirtualscreen = 78;
        internal const int SmCyvirtualscreen = 79;
        internal static readonly nint DpiAwarenessContextPerMonitorAwareV2 = new(-4);

        internal delegate bool MonitorEnumProc(nint hMonitor, nint hdcMonitor, nint lprcMonitor, nint dwData);

        [StructLayout(LayoutKind.Sequential)]
        internal struct RectStruct
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct MonitorInfoEx
        {
            public int cbSize;
            public RectStruct rcMonitor;
            public RectStruct rcWork;
            public uint dwFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string szDevice;
        }

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool EnumDisplayMonitors(nint hdc, nint lprcClip, MonitorEnumProc lpfnEnum, nint dwData);

        [DllImport("user32.dll")]
        internal static extern nint SetThreadDpiAwarenessContext(nint dpiContext);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetMonitorInfo(nint hMonitor, ref MonitorInfoEx lpmi);

        [DllImport("user32.dll")]
        internal static extern int GetSystemMetrics(int nIndex);

        internal static bool TryGetMonitorBounds(nint hMonitor, out System.Drawing.Rectangle bounds)
        {
            var monitorInfo = new MonitorInfoEx
            {
                cbSize = Marshal.SizeOf<MonitorInfoEx>(),
                szDevice = string.Empty
            };

            if (!GetMonitorInfo(hMonitor, ref monitorInfo))
            {
                bounds = default;
                return false;
            }

            bounds = new System.Drawing.Rectangle(
                monitorInfo.rcMonitor.Left,
                monitorInfo.rcMonitor.Top,
                monitorInfo.rcMonitor.Right - monitorInfo.rcMonitor.Left,
                monitorInfo.rcMonitor.Bottom - monitorInfo.rcMonitor.Top);
            return true;
        }
    }
}
