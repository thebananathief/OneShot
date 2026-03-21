using Microsoft.Win32;

namespace OneShot.Services;

public sealed class StartupService
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string AppName = "OneShot";

    public void EnsureRunAtLogin()
    {
        string? exePath = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(exePath))
        {
            return;
        }

        using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, true) ?? Registry.CurrentUser.CreateSubKey(RunKeyPath);
        string value = $"\"{exePath}\"";
        var existing = key.GetValue(AppName) as string;
        if (!string.Equals(existing, value, StringComparison.OrdinalIgnoreCase))
        {
            key.SetValue(AppName, value);
        }
    }
}
