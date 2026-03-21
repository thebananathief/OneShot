using System.Text.Json;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class SnapshotTraceRecorderTests
{
    [Fact]
    public void Record_AppendsValidJsonLines()
    {
        string logDir = CreateTempDir();
        var recorder = new SnapshotTraceRecorder("daemon", logDir);

        recorder.Record("inv-1", "phase_a", 12, new { Foo = "bar" });

        File.Exists(recorder.CurrentPath).Should().BeTrue();
        string line = File.ReadAllLines(recorder.CurrentPath).Single();
        using var document = JsonDocument.Parse(line);
        document.RootElement.GetProperty("InvocationId").GetString().Should().Be("inv-1");
        document.RootElement.GetProperty("ProcessRole").GetString().Should().Be("daemon");
        document.RootElement.GetProperty("Phase").GetString().Should().Be("phase_a");
    }

    [Fact]
    public void Record_RotatesWhenFileExceedsLimit()
    {
        string logDir = CreateTempDir();
        var recorder = new SnapshotTraceRecorder("daemon", logDir, maxFileBytes: 200);

        recorder.Record("inv-1", "phase_a", 1, new { Text = new string('a', 150) });
        recorder.Record("inv-2", "phase_b", 2, new { Text = new string('b', 150) });

        string previousPath = Path.Combine(logDir, "snapshot-trace.previous.jsonl");
        File.Exists(previousPath).Should().BeTrue();
        File.ReadAllText(previousPath).Should().Contain("inv-1");
        File.ReadAllText(recorder.CurrentPath).Should().Contain("inv-2");
    }

    private static string CreateTempDir()
    {
        string path = Path.Combine(Path.GetTempPath(), "OneShot.Tests", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }
}
