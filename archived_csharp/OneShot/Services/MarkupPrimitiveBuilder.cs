using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Media;
using OneShot.Models;

namespace OneShot.Services;

public sealed class MarkupPrimitiveBuilder
{
    public IReadOnlyList<MarkupPrimitive> Build(
        IEnumerable<Control> controls,
        int pixelWidth,
        int pixelHeight,
        double sourceWidthDip,
        double sourceHeightDip)
    {
        var primitives = new List<MarkupPrimitive>();

        foreach (var child in controls)
        {
            switch (child)
            {
                case Polyline polyline when polyline.Points.Count > 1:
                {
                    var color = GetColor(polyline.Stroke);
                    if (color is null)
                    {
                        break;
                    }

                    primitives.Add(new PenStrokePrimitive(
                        polyline.Points.Select(point => ToPixelPoint(point, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip)).ToArray(),
                        color.Value,
                        ToPixelStrokeWidth(polyline.StrokeThickness, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip)));
                    break;
                }
                case Line line:
                {
                    var color = GetColor(line.Stroke);
                    if (color is null)
                    {
                        break;
                    }

                    primitives.Add(new LinePrimitive(
                        ToPixelPoint(line.StartPoint, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        ToPixelPoint(line.EndPoint, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        color.Value,
                        ToPixelStrokeWidth(line.StrokeThickness, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip)));
                    break;
                }
                case Rectangle rectangle:
                {
                    var strokeColor = GetColor(rectangle.Stroke);
                    if (strokeColor is null)
                    {
                        break;
                    }

                    primitives.Add(new RectanglePrimitive(
                        ToPixelRect(new Rect(GetCanvasLeft(rectangle), GetCanvasTop(rectangle), rectangle.Width, rectangle.Height), pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        strokeColor.Value,
                        ToPixelStrokeWidth(rectangle.StrokeThickness, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        GetColor(rectangle.Fill)));
                    break;
                }
                case Ellipse ellipse:
                {
                    var strokeColor = GetColor(ellipse.Stroke);
                    if (strokeColor is null)
                    {
                        break;
                    }

                    primitives.Add(new EllipsePrimitive(
                        ToPixelRect(new Rect(GetCanvasLeft(ellipse), GetCanvasTop(ellipse), ellipse.Width, ellipse.Height), pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        strokeColor.Value,
                        ToPixelStrokeWidth(ellipse.StrokeThickness, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        GetColor(ellipse.Fill)));
                    break;
                }
                case Polygon polygon when polygon.Points.Count > 1:
                {
                    var strokeColor = GetColor(polygon.Stroke) ?? GetColor(polygon.Fill);
                    if (strokeColor is null)
                    {
                        break;
                    }

                    primitives.Add(new PolygonPrimitive(
                        polygon.Points.Select(point => ToPixelPoint(point, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip)).ToArray(),
                        strokeColor.Value,
                        ToPixelStrokeWidth(polygon.StrokeThickness, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        GetColor(polygon.Fill)));
                    break;
                }
                case TextBlock text:
                {
                    var color = GetColor(text.Foreground);
                    if (color is null || string.IsNullOrEmpty(text.Text))
                    {
                        break;
                    }

                    primitives.Add(new TextPrimitive(
                        text.Text,
                        ToPixelPoint(new Point(GetCanvasLeft(text), GetCanvasTop(text)), pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip),
                        text.FontFamily.Name,
                        ToPixelFontSize(text.FontSize, pixelHeight, sourceHeightDip),
                        color.Value));
                    break;
                }
            }
        }

        return primitives;
    }

    private static Color? GetColor(IBrush? brush)
    {
        return brush is ISolidColorBrush solid ? solid.Color : null;
    }

    private static double GetCanvasLeft(Control element)
    {
        double value = Canvas.GetLeft(element);
        return double.IsNaN(value) ? 0 : value;
    }

    private static double GetCanvasTop(Control element)
    {
        double value = Canvas.GetTop(element);
        return double.IsNaN(value) ? 0 : value;
    }

    private static Point ToPixelPoint(Point dipPoint, int pixelWidth, int pixelHeight, double sourceWidthDip, double sourceHeightDip)
    {
        double scaleX = pixelWidth / sourceWidthDip;
        double scaleY = pixelHeight / sourceHeightDip;
        return new Point(dipPoint.X * scaleX, dipPoint.Y * scaleY);
    }

    private static Rect ToPixelRect(Rect dipRect, int pixelWidth, int pixelHeight, double sourceWidthDip, double sourceHeightDip)
    {
        var topLeft = ToPixelPoint(dipRect.TopLeft, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip);
        var bottomRight = ToPixelPoint(dipRect.BottomRight, pixelWidth, pixelHeight, sourceWidthDip, sourceHeightDip);
        return new Rect(topLeft, bottomRight);
    }

    private static double ToPixelStrokeWidth(double dipStrokeWidth, int pixelWidth, int pixelHeight, double sourceWidthDip, double sourceHeightDip)
    {
        double scaleX = pixelWidth / sourceWidthDip;
        double scaleY = pixelHeight / sourceHeightDip;
        return dipStrokeWidth * ((scaleX + scaleY) / 2.0);
    }

    private static double ToPixelFontSize(double dipFontSize, int pixelHeight, double sourceHeightDip)
    {
        double scaleY = pixelHeight / sourceHeightDip;
        return dipFontSize * scaleY;
    }
}
