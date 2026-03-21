using System.IO;
using System.IO.Pipes;
using System.Diagnostics;
using OneShot.Models;

namespace OneShot.Services;

internal sealed class NamedPipeCommandClient : IDisposable
{
    private const int ConnectTimeoutMs = 100;
    private const int MaxAttempts = 6;
    private const int RetryDelayBaseMs = 40;
    private const int RetryDelayStepMs = 20;
    private readonly string _pipeName;
    private readonly Action<string>? _log;
    private readonly SnapshotTraceRecorder? _traceRecorder;

    public NamedPipeCommandClient(SnapshotTraceRecorder? traceRecorder = null, string? pipeName = null, Action<string>? log = null)
    {
        _pipeName = string.IsNullOrWhiteSpace(pipeName) ? NamedPipeCommandServer.DefaultPipeName : pipeName;
        _traceRecorder = traceRecorder;
        _log = log;
    }

    public async Task SendAsync(AppCommand command, CancellationToken cancellationToken = default)
    {
        await SendAsync(AppCommandEnvelope.Create(command), cancellationToken);
    }

    public async Task SendAsync(AppCommandEnvelope envelope, CancellationToken cancellationToken = default)
    {
        if (envelope.Command == AppCommand.None)
        {
            throw new ArgumentOutOfRangeException(nameof(envelope), "Cannot send a no-op command.");
        }

        var stopwatch = Stopwatch.StartNew();
        Exception? lastError = null;
        for (int attempt = 1; attempt <= MaxAttempts; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                using var client = new NamedPipeClientStream(".", _pipeName, PipeDirection.Out);
                _traceRecorder?.Record(envelope.InvocationId, "pipe_connect_start", stopwatch.ElapsedMilliseconds, new { Attempt = attempt });
                Log($"Named pipe send attempt {attempt}/{MaxAttempts} for '{envelope.Command}' started at {stopwatch.ElapsedMilliseconds}ms.");
                await client.ConnectAsync(ConnectTimeoutMs, cancellationToken);
                _traceRecorder?.Record(envelope.InvocationId, "pipe_connect_complete", stopwatch.ElapsedMilliseconds, new { Attempt = attempt });
                using var writer = new StreamWriter(client) { AutoFlush = true };
                await writer.WriteLineAsync(envelope.ToPayload());
                _traceRecorder?.Record(envelope.InvocationId, "pipe_send_complete", stopwatch.ElapsedMilliseconds);
                Log($"Named pipe send for '{envelope.Command}' completed in {stopwatch.ElapsedMilliseconds}ms.");
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
            $"Failed to send '{envelope.Command}' to pipe '{_pipeName}' after {MaxAttempts} attempts in {stopwatch.ElapsedMilliseconds}ms.",
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
