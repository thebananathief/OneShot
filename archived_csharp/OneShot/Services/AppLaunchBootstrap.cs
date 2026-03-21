using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

internal static class AppLaunchBootstrap
{
    internal const string MutexName = "OneShot.Singleton.v1";
    private static AppCommandEnvelope? _pendingInitialEnvelope;

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
        SnapshotTraceRecorder? traceRecorder = null,
        Action<string>? log = null,
        Func<MutexAcquisition>? mutexFactory = null,
        Func<AppCommandEnvelope, CancellationToken, Task>? commandForwarder = null,
        CancellationToken cancellationToken = default)
    {
        var stopwatch = Stopwatch.StartNew();
        var command = ParseCommand(args);
        var envelope = command == AppCommand.None ? null : AppCommandEnvelope.Create(command);
        if (envelope is not null)
        {
            traceRecorder?.Record(envelope.InvocationId, "process_start", 0, new { Args = args });
            traceRecorder?.Record(envelope.InvocationId, "command_parsed", stopwatch.ElapsedMilliseconds, new { Command = envelope.Command.ToString() });
        }

        var acquisition = (mutexFactory ?? AcquireMutex)();
        Log(log, $"Launch command '{command}' parsed; primary={acquisition.IsPrimary}; elapsed={stopwatch.ElapsedMilliseconds}ms.");
        if (envelope is not null)
        {
            traceRecorder?.Record(envelope.InvocationId, "mutex_arbitration_complete", stopwatch.ElapsedMilliseconds, new { acquisition.IsPrimary });
        }

        if (acquisition.IsPrimary)
        {
            _pendingInitialEnvelope = envelope;
            return AppLaunchContext.Primary(acquisition.InstanceMutex, envelope);
        }

        try
        {
            if (envelope is not null)
            {
                traceRecorder?.Record(envelope.InvocationId, "forward_start", stopwatch.ElapsedMilliseconds);
                var forwardAsync = commandForwarder ?? ((appCommandEnvelope, token) => ForwardCommandAsync(appCommandEnvelope, traceRecorder, log, token));
                await forwardAsync(envelope, cancellationToken);
                Log(log, $"Secondary instance forwarded '{envelope.Command}' in {stopwatch.ElapsedMilliseconds}ms.");
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

        return AppLaunchContext.Secondary(envelope);
    }

    private static MutexAcquisition AcquireMutex()
    {
        var instanceMutex = new Mutex(true, MutexName, out bool isPrimary);
        return new MutexAcquisition(instanceMutex, isPrimary);
    }

    private static async Task ForwardCommandAsync(AppCommandEnvelope envelope, SnapshotTraceRecorder? traceRecorder, Action<string>? log, CancellationToken cancellationToken)
    {
        using var client = new NamedPipeCommandClient(traceRecorder, log: log);
        await client.SendAsync(envelope, cancellationToken);
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

    internal static AppCommandEnvelope? ConsumeInitialEnvelope()
    {
        var envelope = _pendingInitialEnvelope;
        _pendingInitialEnvelope = null;
        return envelope;
    }
}

internal readonly record struct MutexAcquisition(Mutex? InstanceMutex, bool IsPrimary);

internal sealed class AppLaunchContext : IDisposable
{
    private readonly Mutex? _instanceMutex;

    private AppLaunchContext(Mutex? instanceMutex, AppCommandEnvelope? initialEnvelope, bool shouldStartApp)
    {
        _instanceMutex = instanceMutex;
        InitialEnvelope = initialEnvelope;
        ShouldStartApp = shouldStartApp;
    }

    public AppCommandEnvelope? InitialEnvelope { get; }

    public bool ShouldStartApp { get; }

    public static AppLaunchContext Primary(Mutex? instanceMutex, AppCommandEnvelope? initialEnvelope)
    {
        return new AppLaunchContext(instanceMutex, initialEnvelope, shouldStartApp: true);
    }

    public static AppLaunchContext Secondary(AppCommandEnvelope? initialEnvelope)
    {
        return new AppLaunchContext(instanceMutex: null, initialEnvelope, shouldStartApp: false);
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
