using System.IO;
using System.IO.Pipes;
using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

public sealed class NamedPipeCommandClient : IDisposable
{
    private const int ConnectTimeoutMs = 1500;
    private const int MaxAttempts = 3;
    private readonly string _pipeName;
    private readonly Action<string>? _log;

    public NamedPipeCommandClient(string? pipeName = null, Action<string>? log = null)
    {
        _pipeName = string.IsNullOrWhiteSpace(pipeName) ? NamedPipeCommandServer.DefaultPipeName : pipeName;
        _log = log;
    }

    public async Task SendAsync(AppCommand command, CancellationToken cancellationToken = default)
    {
        if (command == AppCommand.None)
        {
            throw new ArgumentOutOfRangeException(nameof(command), "Cannot send a no-op command.");
        }

        Exception? lastError = null;
        for (int attempt = 1; attempt <= MaxAttempts; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                using var client = new NamedPipeClientStream(".", _pipeName, PipeDirection.Out);
                await client.ConnectAsync(ConnectTimeoutMs, cancellationToken);
                using var writer = new StreamWriter(client) { AutoFlush = true };
                await writer.WriteLineAsync(command.ToString());
                return;
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex) when (ex is IOException || ex is TimeoutException)
            {
                lastError = ex;
                Log($"Named pipe send failed (attempt {attempt}/{MaxAttempts}): {ex.Message}");
                if (attempt < MaxAttempts)
                {
                    await Task.Delay(TimeSpan.FromMilliseconds(120 * attempt), cancellationToken);
                }
            }
        }

        throw new IOException($"Failed to send '{command}' to pipe '{_pipeName}' after {MaxAttempts} attempts.", lastError);
    }

    public void Dispose()
    {
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
