using OneShot.Models;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class AppLaunchBootstrapTests
{
    [Fact]
    public async Task InitializeAsync_ForwardsSnapshot_ForSecondaryInstance()
    {
        AppCommandEnvelope? forwardedEnvelope = null;

        using var launchContext = await AppLaunchBootstrap.InitializeAsync(
            new[] { "snapshot" },
            mutexFactory: () => new MutexAcquisition(new Mutex(), false),
            commandForwarder: (envelope, _) =>
            {
                forwardedEnvelope = envelope;
                return Task.CompletedTask;
            });

        launchContext.ShouldStartApp.Should().BeFalse();
        launchContext.InitialEnvelope.Should().NotBeNull();
        launchContext.InitialEnvelope!.Command.Should().Be(AppCommand.Snapshot);
        forwardedEnvelope.Should().NotBeNull();
        forwardedEnvelope!.Command.Should().Be(AppCommand.Snapshot);
        forwardedEnvelope.InvocationId.Should().NotBeNullOrWhiteSpace();
    }

    [Fact]
    public async Task InitializeAsync_DoesNotForwardUnsupportedCommand_ForSecondaryInstance()
    {
        bool forwarded = false;

        using var launchContext = await AppLaunchBootstrap.InitializeAsync(
            new[] { "invalid" },
            mutexFactory: () => new MutexAcquisition(new Mutex(), false),
            commandForwarder: (_, _) =>
            {
                forwarded = true;
                return Task.CompletedTask;
            });

        launchContext.ShouldStartApp.Should().BeFalse();
        launchContext.InitialEnvelope.Should().BeNull();
        forwarded.Should().BeFalse();
    }

    [Fact]
    public async Task InitializeAsync_ReturnsPrimaryContext_ForPrimaryInstance()
    {
        using var launchContext = await AppLaunchBootstrap.InitializeAsync(
            new[] { "snapshot" },
            mutexFactory: () => new MutexAcquisition(new Mutex(), true),
            commandForwarder: (_, _) => Task.CompletedTask);

        launchContext.ShouldStartApp.Should().BeTrue();
        launchContext.InitialEnvelope.Should().NotBeNull();
        launchContext.InitialEnvelope!.Command.Should().Be(AppCommand.Snapshot);
    }
}
