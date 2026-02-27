using System.IO.Pipes;
using System.Text;
using System.Threading.Tasks;
using FluentAssertions;
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
        var received = new TaskCompletionSource<AppCommand>(TaskCreationOptions.RunContinuationsAsynchronously);
        server.CommandReceived += (_, command) => received.TrySetResult(command);
        var serverTask = server.StartAsync();

        using var client = new NamedPipeCommandClient(pipeName);
        await client.SendAsync(AppCommand.Snapshot);

        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Should().Be(AppCommand.Snapshot);

        server.Dispose();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(3));
    }

    [Fact]
    public async Task Server_IgnoresInvalidCommand_AndContinuesListening()
    {
        string pipeName = $"OneShot.Tests.{Guid.NewGuid():N}";
        var server = new NamedPipeCommandServer(pipeName);
        var received = new TaskCompletionSource<AppCommand>(TaskCreationOptions.RunContinuationsAsynchronously);
        server.CommandReceived += (_, command) => received.TrySetResult(command);
        var serverTask = server.StartAsync();

        await SendRawAsync(pipeName, "invalid-command");

        using var client = new NamedPipeCommandClient(pipeName);
        await client.SendAsync(AppCommand.Snapshot);

        var command = await received.Task.WaitAsync(TimeSpan.FromSeconds(3));
        command.Should().Be(AppCommand.Snapshot);

        server.Dispose();
        await serverTask.WaitAsync(TimeSpan.FromSeconds(3));
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
