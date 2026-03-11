using Avalonia;
using OneShot.Services;

namespace OneShot.Tests;

public sealed class NotificationPlacementTests
{
    [Fact]
    public void Arrange_At100PercentScaling_KeepsRightMarginInsideWorkingArea()
    {
        var workingArea = new PixelRect(0, 0, 1280, 800);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 220, 88),
            1.0,
            16,
            16,
            8);

        placement.Position.Should().Be(new PixelPoint(1044, 16));
        placement.SizePixels.Should().Be(new PixelSize(220, 88));
        placement.NextTopInPixels.Should().Be(112);
    }

    [Theory]
    [InlineData(1.25, 275, 110)]
    [InlineData(1.5, 330, 132)]
    [InlineData(2.0, 440, 176)]
    public void Arrange_WithScaledDisplay_UsesPhysicalPixelWindowSize(double scaling, int expectedWidth, int expectedHeight)
    {
        var workingArea = new PixelRect(0, 0, 1280, 800);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 220, 88),
            scaling,
            16,
            16,
            8);

        placement.SizePixels.Should().Be(new PixelSize(expectedWidth, expectedHeight));
        placement.Position.X.Should().Be(1280 - expectedWidth - 16);
        (placement.Position.X + placement.SizePixels.Width).Should().Be(1264);
    }

    [Fact]
    public void Arrange_WhenBoundsAreUnknown_UsesScaledFallbackSize()
    {
        var workingArea = new PixelRect(0, 0, 1280, 800);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 0, 0),
            2.0,
            16,
            16,
            8);

        placement.SizePixels.Should().Be(new PixelSize(440, 176));
        placement.Position.Should().Be(new PixelPoint(824, 16));
    }

    [Fact]
    public void Arrange_WithNonZeroWorkingAreaOrigin_PreservesOriginInPlacement()
    {
        var workingArea = new PixelRect(100, 50, 1200, 900);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 220, 88),
            1.5,
            66,
            16,
            8);

        placement.Position.Should().Be(new PixelPoint(954, 66));
        (placement.Position.X + placement.SizePixels.Width).Should().Be(1284);
    }
}
