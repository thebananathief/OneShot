using System.IO;
using System.IO.Pipes;
using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

public sealed class NamedPipeCommandServer : IDisposable
{
    public const string DefaultPipeName = "OneShot.Command.v1";
    private readonly CancellationTokenSource _cts = new();
    private readonly string _pipeName;
    private readonly Action<string>? _log;

    public event EventHandler<AppCommand>? CommandReceived;

    public NamedPipeCommandServer(string? pipeName = null, Action<string>? log = null)
    {
        _pipeName = string.IsNullOrWhiteSpace(pipeName) ? DefaultPipeName : pipeName;
        _log = log;
    }

    public async Task StartAsync()
    {
        NamedPipeServerStream? currentServer = null;
        try
        {
            currentServer = CreateServer();
            Task waitForConnection = currentServer.WaitForConnectionAsync(_cts.Token);

            while (!_cts.Token.IsCancellationRequested)
            {
                try
                {
                    await waitForConnection;
                }
                catch (OperationCanceledException)
                {
                    break;
                }

                NamedPipeServerStream? nextServer = null;
                Task? nextWaitForConnection = null;

                try
                {
                    nextServer = CreateServer();
                    nextWaitForConnection = nextServer.WaitForConnectionAsync(_cts.Token);
                    await HandleConnectionAsync(currentServer, _cts.Token);
                }
                catch (OperationCanceledException)
                {
                    nextServer?.Dispose();
                    break;
                }
                catch (Exception ex)
                {
                    nextServer?.Dispose();
                    Log($"Named pipe server error: {ex}");
                }
                finally
                {
                    currentServer.Dispose();
                }

                if (nextServer is null || nextWaitForConnection is null)
                {
                    currentServer = CreateServer();
                    waitForConnection = currentServer.WaitForConnectionAsync(_cts.Token);
                    continue;
                }

                currentServer = nextServer;
                waitForConnection = nextWaitForConnection;
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            Log($"Named pipe server error: {ex}");
        }
        finally
        {
            currentServer?.Dispose();
        }
    }

    private NamedPipeServerStream CreateServer()
    {
        return new NamedPipeServerStream(
            _pipeName,
            PipeDirection.In,
            NamedPipeServerStream.MaxAllowedServerInstances,
            PipeTransmissionMode.Message,
            PipeOptions.Asynchronous);
    }

    private async Task HandleConnectionAsync(NamedPipeServerStream server, CancellationToken cancellationToken)
    {
        using var reader = new StreamReader(server);
        var line = await reader.ReadLineAsync(cancellationToken);
        if (string.IsNullOrWhiteSpace(line))
        {
            Log("Received empty command payload.");
            return;
        }

        Log($"Received command payload '{line}'.");
        if (Enum.TryParse<AppCommand>(line, true, out var command) && command != AppCommand.None)
        {
            CommandReceived?.Invoke(this, command);
            return;
        }

        Log($"Received invalid command payload: '{line}'.");
    }

    public void Dispose()
    {
        _cts.Cancel();
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
}
