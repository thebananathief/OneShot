using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using OneShot.Models;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class NamedPipeCommandTests
{
    [Fact]
    public async Task SendAsync_DeliversCommandToServer()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var server = new NamedPipeCommandServer(pipeName);
        var received = new TaskCompletionSource<AppCommandEnvelope>(TaskCreationOptions.RunContinuationsAsynchronously);
        server.CommandReceived += (_, command) => received.TrySetResult(command);
        var serverTask = server.StartAsync();

        using var client = new NamedPipeCommandClient(pipeName: pipeName);
        await client.SendAsync(AppCommand.Snapshot);

        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Command.Should().Be(AppCommand.Snapshot);
        command.InvocationId.Should().NotBeNullOrWhiteSpace();

        server.Dispose();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(3));
    }

    [Fact]
    public async Task Server_IgnoresInvalidCommand_AndContinuesListening()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var server = new NamedPipeCommandServer(pipeName);
        var received = new TaskCompletionSource<AppCommandEnvelope>(TaskCreationOptions.RunContinuationsAsynchronously);
        server.CommandReceived += (_, command) => received.TrySetResult(command);
        var serverTask = server.StartAsync();

        await SendRawAsync(pipeName, "invalid-command");

        using var client = new NamedPipeCommandClient(pipeName: pipeName);
        await client.SendAsync(AppCommand.Snapshot);

        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Command.Should().Be(AppCommand.Snapshot);

        server.Dispose();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(3));
    }

    [Fact]
    public async Task SendAsync_RetriesUntilServerBecomesAvailable_WithinBoundedBudget()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var received = new TaskCompletionSource<AppCommandEnvelope>(TaskCreationOptions.RunContinuationsAsynchronously);
        var serverStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        _ = Task.Run(async () =>
        {
            await Task.Delay(250);
            var server = new NamedPipeCommandServer(pipeName);
            server.CommandReceived += (_, command) =>
            {
                received.TrySetResult(command);
                server.Dispose();
            };

            serverStarted.TrySetResult();
            await server.StartAsync();
        });

        var stopwatch = Stopwatch.StartNew();
        using var client = new NamedPipeCommandClient(pipeName: pipeName);
        await client.SendAsync(AppCommand.Snapshot);

        await serverStarted.Task.WaitAsync(TimeSpan.FromSeconds(3));
        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Command.Should().Be(AppCommand.Snapshot);
        stopwatch.Elapsed.Should().BeLessThan(TimeSpan.FromMilliseconds(1200));
    }

    [Fact]
    public async Task Server_AcceptsLegacyPlainTextCommandPayload()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var server = new NamedPipeCommandServer(pipeName);
        var received = new TaskCompletionSource<AppCommandEnvelope>(TaskCreationOptions.RunContinuationsAsynchronously);
        server.CommandReceived += (_, command) => received.TrySetResult(command);
        var serverTask = server.StartAsync();

        await SendRawAsync(pipeName, "Snapshot");

        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Command.Should().Be(AppCommand.Snapshot);
        command.TraceEnabled.Should().BeFalse();
        command.InvocationId.Should().NotBeNullOrWhiteSpace();

        server.Dispose();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(3));
    }

    [Fact]
    public async Task SendAsync_FailsFast_WhenServerIsUnavailable()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var stopwatch = Stopwatch.StartNew();

        using var client = new NamedPipeCommandClient(pipeName: pipeName);
        Func<Task> act = async () => await client.SendAsync(AppCommand.Snapshot);

        await act.Should().ThrowAsync<IOException>();
        stopwatch.Elapsed.Should().BeLessThan(TimeSpan.FromMilliseconds(1200));
    }

    private static async Task SendRawAsync(string pipeName, string payload)
    {
        using var client = new NamedPipeClientStream(".", pipeName, PipeDirection.Out);
        await client.ConnectAsync(1500);
        byte[] bytes = Encoding.UTF8.GetBytes(payload + Environment.NewLine);
        await client.WriteAsync(bytes, 0, bytes.Length);
        await client.FlushAsync();
    }
}
