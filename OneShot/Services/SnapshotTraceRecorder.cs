using System.Text.Json;

namespace OneShot.Services;

internal sealed class SnapshotTraceRecorder
{
    private const long DefaultMaxFileBytes = 1024 * 1024;
    private const string FileMutexName = "OneShot.SnapshotTraceFile.v1";
    private readonly string _processRole;
    private readonly string _logDir;
    private readonly string _currentPath;
    private readonly string _previousPath;
    private readonly long _maxFileBytes;

    public SnapshotTraceRecorder(string processRole, string? logDir = null, long maxFileBytes = DefaultMaxFileBytes)
    {
        _processRole = processRole;
        _logDir = string.IsNullOrWhiteSpace(logDir)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OneShot", "Logs")
            : logDir;
        _currentPath = Path.Combine(_logDir, "snapshot-trace.jsonl");
        _previousPath = Path.Combine(_logDir, "snapshot-trace.previous.jsonl");
        _maxFileBytes = maxFileBytes;
    }

    public string CurrentPath => _currentPath;

    public void Record(string invocationId, string phase, long elapsedMs, object? details = null)
    {
        if (string.IsNullOrWhiteSpace(invocationId))
        {
            return;
        }

        Directory.CreateDirectory(_logDir);
        string line = JsonSerializer.Serialize(new SnapshotTraceEvent(
            DateTimeOffset.UtcNow,
            invocationId,
            _processRole,
            phase,
            elapsedMs,
            details));

        using var fileMutex = new Mutex(false, FileMutexName);
        try
        {
            fileMutex.WaitOne();
            RotateIfNeeded(line);
            File.AppendAllText(_currentPath, line + Environment.NewLine);
        }
        finally
        {
            try
            {
                fileMutex.ReleaseMutex();
            }
            catch
            {
            }
        }
    }

    private void RotateIfNeeded(string line)
    {
        if (File.Exists(_currentPath))
        {
            var currentLength = new FileInfo(_currentPath).Length;
            if (currentLength + line.Length + Environment.NewLine.Length <= _maxFileBytes)
            {
                return;
            }

            if (File.Exists(_previousPath))
            {
                File.Delete(_previousPath);
            }

            File.Move(_currentPath, _previousPath);
        }
    }

    private sealed record SnapshotTraceEvent(
        DateTimeOffset TimestampUtc,
        string InvocationId,
        string ProcessRole,
        string Phase,
        long ElapsedMs,
        object? Details);
}
