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
        while (!_cts.Token.IsCancellationRequested)
        {
            try
            {
                using var server = new NamedPipeServerStream(_pipeName, PipeDirection.In, 1, PipeTransmissionMode.Message, PipeOptions.Asynchronous);
                await server.WaitForConnectionAsync(_cts.Token);
                using var reader = new StreamReader(server);
                var line = await reader.ReadLineAsync(_cts.Token);
                if (string.IsNullOrWhiteSpace(line))
                {
                    Log("Received empty command payload.");
                    continue;
                }

                if (Enum.TryParse<AppCommand>(line, true, out var command) && command != AppCommand.None)
                {
                    CommandReceived?.Invoke(this, command);
                    continue;
                }

                Log($"Received invalid command payload: '{line}'.");
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Log($"Named pipe server error: {ex}");
            }
        }
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
