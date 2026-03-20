using System.IO;
using System.IO.Pipes;
using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

public sealed class NamedPipeCommandClient : IDisposable
{
    private const int ConnectTimeoutMs = 100;
    private const int MaxAttempts = 6;
    private const int RetryDelayBaseMs = 40;
    private const int RetryDelayStepMs = 20;
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

        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        for (int attempt = 1; attempt <= MaxAttempts; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                using var client = new NamedPipeClientStream(".", _pipeName, PipeDirection.Out);
                Log($"Named pipe send attempt {attempt}/{MaxAttempts} for '{command}' started at {stopwatch.ElapsedMilliseconds}ms.");
                await client.ConnectAsync(ConnectTimeoutMs, cancellationToken);
                using var writer = new StreamWriter(client) { AutoFlush = true };
                await writer.WriteLineAsync(command.ToString());
                Log($"Named pipe send for '{command}' completed in {stopwatch.ElapsedMilliseconds}ms.");
                return;
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex) when (ex is IOException || ex is TimeoutException)
            {
                lastError = ex;
                Log($"Named pipe send failed (attempt {attempt}/{MaxAttempts}) after {stopwatch.ElapsedMilliseconds}ms: {ex.Message}");
                if (attempt < MaxAttempts)
                {
                    int retryDelayMs = RetryDelayBaseMs + ((attempt - 1) * RetryDelayStepMs);
                    await Task.Delay(TimeSpan.FromMilliseconds(retryDelayMs), cancellationToken);
                }
            }
        }

        throw new IOException(
            $"Failed to send '{command}' to pipe '{_pipeName}' after {MaxAttempts} attempts in {stopwatch.ElapsedMilliseconds}ms.",
            lastError);
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
