using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

internal static class AppLaunchBootstrap
{
    internal const string MutexName = "OneShot.Singleton.v1";

    internal static AppCommand ParseCommand(string[] args)
    {
        if (args.Length == 0)
        {
            return AppCommand.None;
        }

        return args[0].ToLowerInvariant() switch
        {
            "snapshot" => AppCommand.Snapshot,
            _ => AppCommand.None
        };
    }

    internal static async Task<AppLaunchContext> InitializeAsync(
        string[] args,
        Action<string>? log = null,
        Func<MutexAcquisition>? mutexFactory = null,
        Func<AppCommand, CancellationToken, Task>? commandForwarder = null,
        CancellationToken cancellationToken = default)
    {
        var stopwatch = Stopwatch.StartNew();
        var command = ParseCommand(args);
        var acquisition = (mutexFactory ?? AcquireMutex)();
        Log(log, $"Launch command '{command}' parsed; primary={acquisition.IsPrimary}; elapsed={stopwatch.ElapsedMilliseconds}ms.");

        if (acquisition.IsPrimary)
        {
            return AppLaunchContext.Primary(acquisition.InstanceMutex, command);
        }

        try
        {
            if (command != AppCommand.None)
            {
                var forwardAsync = commandForwarder ?? ((appCommand, token) => ForwardCommandAsync(appCommand, log, token));
                await forwardAsync(command, cancellationToken);
                Log(log, $"Secondary instance forwarded '{command}' in {stopwatch.ElapsedMilliseconds}ms.");
            }
            else
            {
                Log(log, $"Secondary instance exiting with no supported command after {stopwatch.ElapsedMilliseconds}ms.");
            }
        }
        finally
        {
            acquisition.InstanceMutex?.Dispose();
        }

        return AppLaunchContext.Secondary(command);
    }

    private static MutexAcquisition AcquireMutex()
    {
        var instanceMutex = new Mutex(true, MutexName, out bool isPrimary);
        return new MutexAcquisition(instanceMutex, isPrimary);
    }

    private static async Task ForwardCommandAsync(AppCommand command, Action<string>? log, CancellationToken cancellationToken)
    {
        using var client = new NamedPipeCommandClient(log: log);
        await client.SendAsync(command, cancellationToken);
    }

    private static void Log(Action<string>? log, string message)
    {
        if (log is not null)
        {
            log(message);
            return;
        }

        Debug.WriteLine(message);
    }
}

internal readonly record struct MutexAcquisition(Mutex? InstanceMutex, bool IsPrimary);

internal sealed class AppLaunchContext : IDisposable
{
    private readonly Mutex? _instanceMutex;

    private AppLaunchContext(Mutex? instanceMutex, AppCommand initialCommand, bool shouldStartApp)
    {
        _instanceMutex = instanceMutex;
        InitialCommand = initialCommand;
        ShouldStartApp = shouldStartApp;
    }

    public AppCommand InitialCommand { get; }

    public bool ShouldStartApp { get; }

    public static AppLaunchContext Primary(Mutex? instanceMutex, AppCommand initialCommand)
    {
        return new AppLaunchContext(instanceMutex, initialCommand, shouldStartApp: true);
    }

    public static AppLaunchContext Secondary(AppCommand initialCommand)
    {
        return new AppLaunchContext(instanceMutex: null, initialCommand, shouldStartApp: false);
    }

    public void Dispose()
    {
        if (_instanceMutex is null)
        {
            return;
        }

        try
        {
            _instanceMutex.ReleaseMutex();
        }
        catch
        {
        }

        _instanceMutex.Dispose();
    }
}
