using Avalonia;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class SnapshotSelectionMapperTests
{
    [Fact]
    public void ToVirtualCaptureRect_FullPrimaryMonitor_MapsToOrigin()
    {
        var virtualScreenBounds = new System.Drawing.Rectangle(0, 0, 1920, 1080);
        var selection = new Rect(0, 0, 1920, 1080);

        var mapped = SnapshotSelectionMapper.ToVirtualCaptureRect(selection, virtualScreenBounds);

        mapped.Should().Be(new Rect(0, 0, 1920, 1080));
    }

    [Fact]
    public void ToVirtualCaptureRect_WithNonZeroOrigin_SubtractsVirtualOffset()
    {
        var virtualScreenBounds = new System.Drawing.Rectangle(100, 50, 1600, 900);
        var selection = new Rect(250, 175, 400, 300);

        var mapped = SnapshotSelectionMapper.ToVirtualCaptureRect(selection, virtualScreenBounds);

        mapped.Should().Be(new Rect(150, 125, 400, 300));
    }

    [Fact]
    public void ToVirtualCaptureRect_WithNegativeCoordinates_PreservesRelativePosition()
    {
        var virtualScreenBounds = new System.Drawing.Rectangle(-1920, 0, 3840, 1080);
        var selection = new Rect(-1600, 120, 500, 400);

        var mapped = SnapshotSelectionMapper.ToVirtualCaptureRect(selection, virtualScreenBounds);

        mapped.Should().Be(new Rect(320, 120, 500, 400));
    }

    [Fact]
    public void ToVirtualCaptureRect_CrossMonitorSelection_SpansAcrossFrozenDesktop()
    {
        var virtualScreenBounds = new System.Drawing.Rectangle(-1920, 0, 3840, 1080);
        var selection = new Rect(-300, 80, 700, 500);

        var mapped = SnapshotSelectionMapper.ToVirtualCaptureRect(selection, virtualScreenBounds);

        mapped.Should().Be(new Rect(1620, 80, 700, 500));
    }
}
