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
            new Rect(0, 0, 258, 119),
            1.0,
            16,
            16,
            8);

        placement.Position.Should().Be(new PixelPoint(1006, 16));
        placement.SizePixels.Should().Be(new PixelSize(258, 119));
        placement.NextTopInPixels.Should().Be(143);
    }

    [Theory]
    [InlineData(1.25, 323, 149)]
    [InlineData(1.5, 387, 179)]
    [InlineData(2.0, 516, 238)]
    public void Arrange_WithScaledDisplay_UsesPhysicalPixelWindowSize(double scaling, int expectedWidth, int expectedHeight)
    {
        var workingArea = new PixelRect(0, 0, 1280, 800);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 258, 119),
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

        placement.SizePixels.Should().Be(new PixelSize(516, 238));
        placement.Position.Should().Be(new PixelPoint(748, 16));
    }

    [Fact]
    public void Arrange_WithNonZeroWorkingAreaOrigin_PreservesOriginInPlacement()
    {
        var workingArea = new PixelRect(100, 50, 1200, 900);

        var placement = NotificationPlacement.Arrange(
            workingArea,
            new Rect(0, 0, 258, 119),
            1.5,
            66,
            16,
            8);

        placement.Position.Should().Be(new PixelPoint(897, 66));
        (placement.Position.X + placement.SizePixels.Width).Should().Be(1284);
    }
}
