using Avalonia;
using FluentAssertions;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class SelectionGeometryTests
{
    [Fact]
    public void BuildRect_NormalizesCoordinatesForAnyDragDirection()
    {
        var rect = SelectionGeometry.BuildRect(new PixelPoint(350, 200), new PixelPoint(100, 50));

        rect.X.Should().Be(100);
        rect.Y.Should().Be(50);
        rect.Width.Should().Be(250);
        rect.Height.Should().Be(150);
    }

    [Fact]
    public void IntersectWithMonitor_WhenSelectionCrossesMonitors_ReturnsMonitorSlice()
    {
        var selection = new Rect(1800, 100, 400, 300);
        var monitorBounds = new System.Drawing.Rectangle(1920, 0, 1920, 1080);

        var intersection = SelectionGeometry.IntersectWithMonitor(selection, monitorBounds);

        intersection.Should().Be(new Rect(1920, 100, 280, 300));
    }

    [Fact]
    public void IntersectWithMonitor_SupportsNegativeCoordinates()
    {
        var selection = new Rect(-1500, 200, 800, 600);
        var monitorBounds = new System.Drawing.Rectangle(-1920, 0, 1920, 1080);

        var intersection = SelectionGeometry.IntersectWithMonitor(selection, monitorBounds);

        intersection.Should().Be(new Rect(-1500, 200, 800, 600));
    }

    [Fact]
    public void IntersectWithMonitor_WhenNoOverlap_ReturnsNull()
    {
        var selection = new Rect(100, 100, 200, 200);
        var monitorBounds = new System.Drawing.Rectangle(1000, 0, 1000, 1000);

        var intersection = SelectionGeometry.IntersectWithMonitor(selection, monitorBounds);

        intersection.Should().BeNull();
    }

    [Theory]
    [InlineData(3, 3, true)]
    [InlineData(2, 3, false)]
    [InlineData(3, 2, false)]
    [InlineData(2, 2, false)]
    public void MeetsMinimumSize_UsesInclusiveThreshold(int width, int height, bool expected)
    {
        var rect = new Rect(10, 10, width, height);

        bool result = SelectionGeometry.MeetsMinimumSize(rect, 3, 3);

        result.Should().Be(expected);
    }
}
