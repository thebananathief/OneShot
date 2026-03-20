using System.Diagnostics;
using Avalonia;
using System.Runtime.InteropServices;
using OneShot.Services;

namespace OneShot;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        var stopwatch = Stopwatch.StartNew();
        Debug.WriteLine($"OneShot process starting; args='{string.Join(' ', args)}'.");
        DpiAwareness.EnablePerMonitorV2();
        using var launchContext = AppLaunchBootstrap.InitializeAsync(args, message => Debug.WriteLine(message)).GetAwaiter().GetResult();
        Debug.WriteLine($"Launch arbitration completed in {stopwatch.ElapsedMilliseconds}ms; shouldStartApp={launchContext.ShouldStartApp}.");
        if (!launchContext.ShouldStartApp)
        {
            return;
        }

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
    {
        return AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
    }

    private static class DpiAwareness
    {
        private const int ErrorAccessDenied = 5;
        private static readonly nint DpiAwarenessContextPerMonitorAwareV2 = new(-4);

        internal static void EnablePerMonitorV2()
        {
            if (TrySetProcessDpiAwarenessContext())
            {
                return;
            }

            if (TrySetProcessDpiAwareness())
            {
                return;
            }

            TrySetProcessDpiAware();
        }

        private static bool TrySetProcessDpiAwarenessContext()
        {
            if (!OperatingSystem.IsWindows())
            {
                return false;
            }

            if (SetProcessDpiAwarenessContext(DpiAwarenessContextPerMonitorAwareV2))
            {
                return true;
            }

            int error = Marshal.GetLastWin32Error();
            if (error == ErrorAccessDenied)
            {
                // DPI awareness was already configured by manifest/framework initialization.
                return true;
            }

            return false;
        }

        private static bool TrySetProcessDpiAwareness()
        {
            if (!OperatingSystem.IsWindows())
            {
                return false;
            }

            try
            {
                int result = SetProcessDpiAwareness(ProcessDpiAwareness.PerMonitorDpiAware);
                return result == 0 || result == ErrorAccessDenied;
            }
            catch (DllNotFoundException)
            {
                return false;
            }
            catch (EntryPointNotFoundException)
            {
                return false;
            }
        }

        private static bool TrySetProcessDpiAware()
        {
            if (!OperatingSystem.IsWindows())
            {
                return false;
            }

            if (SetProcessDPIAware())
            {
                return true;
            }

            int error = Marshal.GetLastWin32Error();
            return error == ErrorAccessDenied;
        }

        private enum ProcessDpiAwareness
        {
            ProcessDpiUnaware = 0,
            ProcessSystemDpiAware = 1,
            PerMonitorDpiAware = 2
        }

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetProcessDpiAwarenessContext(nint dpiAwarenessContext);

        [DllImport("shcore.dll")]
        private static extern int SetProcessDpiAwareness(ProcessDpiAwareness value);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetProcessDPIAware();
    }
}
