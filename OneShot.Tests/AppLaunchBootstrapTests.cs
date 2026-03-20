using OneShot.Models;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class AppLaunchBootstrapTests
{
    [Fact]
    public async Task InitializeAsync_ForwardsSnapshot_ForSecondaryInstance()
    {
        AppCommand? forwardedCommand = null;

        using var launchContext = await AppLaunchBootstrap.InitializeAsync(
            new[] { "snapshot" },
            mutexFactory: () => new MutexAcquisition(new Mutex(), false),
            commandForwarder: (command, _) =>
            {
                forwardedCommand = command;
                return Task.CompletedTask;
            });

        launchContext.ShouldStartApp.Should().BeFalse();
        launchContext.InitialCommand.Should().Be(AppCommand.Snapshot);
        forwardedCommand.Should().Be(AppCommand.Snapshot);
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
        launchContext.InitialCommand.Should().Be(AppCommand.None);
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
        launchContext.InitialCommand.Should().Be(AppCommand.Snapshot);
    }
}
