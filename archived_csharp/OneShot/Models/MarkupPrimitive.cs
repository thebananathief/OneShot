using Avalonia;
using Avalonia.Media;

namespace OneShot.Models;

public abstract record MarkupPrimitive;

public sealed record PenStrokePrimitive(IReadOnlyList<Point> Points, Color StrokeColor, double StrokeThickness) : MarkupPrimitive;

public sealed record LinePrimitive(Point Start, Point End, Color StrokeColor, double StrokeThickness) : MarkupPrimitive;

public sealed record ArrowPrimitive(Point Start, Point End, Color StrokeColor, double StrokeThickness) : MarkupPrimitive;

public sealed record RectanglePrimitive(Rect Bounds, Color StrokeColor, double StrokeThickness, Color? FillColor) : MarkupPrimitive;

public sealed record EllipsePrimitive(Rect Bounds, Color StrokeColor, double StrokeThickness, Color? FillColor) : MarkupPrimitive;

public sealed record PolygonPrimitive(IReadOnlyList<Point> Points, Color StrokeColor, double StrokeThickness, Color? FillColor) : MarkupPrimitive;

public sealed record TextPrimitive(string Text, Point Position, string FontFamilyName, double FontSize, Color FontColor) : MarkupPrimitive;