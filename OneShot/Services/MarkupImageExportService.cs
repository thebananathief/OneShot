using SkiaSharp;
using OneShot.Models;

namespace OneShot.Services;

public sealed class MarkupImageExportService : IMarkupImageExportService
{
    public CapturedImage Export(CapturedImage source, IReadOnlyList<MarkupPrimitive> primitives)
    {
        if (primitives.Count == 0)
        {
            return source;
        }

        using var baseBitmap = SKBitmap.Decode(source.PngBytes);
        if (baseBitmap is null)
        {
            return source;
        }

        using var surface = SKSurface.Create(new SKImageInfo(baseBitmap.Width, baseBitmap.Height, SKColorType.Bgra8888, SKAlphaType.Premul));
        var canvas = surface.Canvas;
        canvas.Clear(SKColors.Transparent);
        canvas.DrawBitmap(baseBitmap, 0, 0);

        foreach (var primitive in primitives)
        {
            DrawPrimitive(canvas, primitive);
        }

        using var image = surface.Snapshot();
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        if (data is null)
        {
            return source;
        }

        byte[] bytes = data.ToArray();
        return new CapturedImage(bytes, DateTimeOffset.UtcNow, baseBitmap.Width, baseBitmap.Height);
    }

    private static void DrawPrimitive(SKCanvas canvas, MarkupPrimitive primitive)
    {
        switch (primitive)
        {
            case PenStrokePrimitive pen:
                DrawPenStroke(canvas, pen);
                break;
            case LinePrimitive line:
                using (var paint = CreateStrokePaint(line.StrokeColor, line.StrokeThickness))
                {
                    canvas.DrawLine((float)line.Start.X, (float)line.Start.Y, (float)line.End.X, (float)line.End.Y, paint);
                }
                break;
            case ArrowPrimitive arrow:
                DrawArrow(canvas, arrow);
                break;
            case RectanglePrimitive rect:
                DrawRectangle(canvas, rect);
                break;
            case EllipsePrimitive ellipse:
                DrawEllipse(canvas, ellipse);
                break;
            case PolygonPrimitive polygon:
                DrawPolygon(canvas, polygon);
                break;
            case TextPrimitive text:
                DrawText(canvas, text);
                break;
        }
    }

    private static void DrawPenStroke(SKCanvas canvas, PenStrokePrimitive primitive)
    {
        if (primitive.Points.Count < 2)
        {
            return;
        }

        using var path = new SKPath();
        path.MoveTo((float)primitive.Points[0].X, (float)primitive.Points[0].Y);
        for (int i = 1; i < primitive.Points.Count; i++)
        {
            path.LineTo((float)primitive.Points[i].X, (float)primitive.Points[i].Y);
        }

        using var paint = CreateStrokePaint(primitive.StrokeColor, primitive.StrokeThickness);
        canvas.DrawPath(path, paint);
    }

    private static void DrawArrow(SKCanvas canvas, ArrowPrimitive primitive)
    {
        using var paint = CreateStrokePaint(primitive.StrokeColor, primitive.StrokeThickness);
        canvas.DrawLine((float)primitive.Start.X, (float)primitive.Start.Y, (float)primitive.End.X, (float)primitive.End.Y, paint);

        var angle = Math.Atan2(primitive.End.Y - primitive.Start.Y, primitive.End.X - primitive.Start.X);
        var headLength = Math.Max(10, primitive.StrokeThickness * 4);
        const double wingAngle = Math.PI / 7;

        var p1 = new SKPoint(
            (float)(primitive.End.X - headLength * Math.Cos(angle - wingAngle)),
            (float)(primitive.End.Y - headLength * Math.Sin(angle - wingAngle)));
        var p2 = new SKPoint(
            (float)(primitive.End.X - headLength * Math.Cos(angle + wingAngle)),
            (float)(primitive.End.Y - headLength * Math.Sin(angle + wingAngle)));

        using var headPath = new SKPath();
        headPath.MoveTo((float)primitive.End.X, (float)primitive.End.Y);
        headPath.LineTo(p1);
        headPath.LineTo(p2);
        headPath.Close();

        using var fillPaint = CreateFillPaint(primitive.StrokeColor);
        canvas.DrawPath(headPath, fillPaint);
        canvas.DrawPath(headPath, paint);
    }

    private static void DrawRectangle(SKCanvas canvas, RectanglePrimitive primitive)
    {
        var rect = new SKRect(
            (float)primitive.Bounds.X,
            (float)primitive.Bounds.Y,
            (float)(primitive.Bounds.X + primitive.Bounds.Width),
            (float)(primitive.Bounds.Y + primitive.Bounds.Height));

        if (primitive.FillColor is not null)
        {
            using var fill = CreateFillPaint(primitive.FillColor.Value);
            canvas.DrawRect(rect, fill);
        }

        using var stroke = CreateStrokePaint(primitive.StrokeColor, primitive.StrokeThickness);
        canvas.DrawRect(rect, stroke);
    }

    private static void DrawEllipse(SKCanvas canvas, EllipsePrimitive primitive)
    {
        float cx = (float)(primitive.Bounds.X + (primitive.Bounds.Width / 2));
        float cy = (float)(primitive.Bounds.Y + (primitive.Bounds.Height / 2));
        float rx = (float)(primitive.Bounds.Width / 2);
        float ry = (float)(primitive.Bounds.Height / 2);

        if (primitive.FillColor is not null)
        {
            using var fill = CreateFillPaint(primitive.FillColor.Value);
            canvas.DrawOval(cx, cy, rx, ry, fill);
        }

        using var stroke = CreateStrokePaint(primitive.StrokeColor, primitive.StrokeThickness);
        canvas.DrawOval(cx, cy, rx, ry, stroke);
    }

    private static void DrawPolygon(SKCanvas canvas, PolygonPrimitive primitive)
    {
        if (primitive.Points.Count < 2)
        {
            return;
        }

        using var path = new SKPath();
        path.MoveTo((float)primitive.Points[0].X, (float)primitive.Points[0].Y);
        for (int i = 1; i < primitive.Points.Count; i++)
        {
            path.LineTo((float)primitive.Points[i].X, (float)primitive.Points[i].Y);
        }

        path.Close();

        if (primitive.FillColor is not null)
        {
            using var fill = CreateFillPaint(primitive.FillColor.Value);
            canvas.DrawPath(path, fill);
        }

        using var stroke = CreateStrokePaint(primitive.StrokeColor, primitive.StrokeThickness);
        canvas.DrawPath(path, stroke);
    }

    private static void DrawText(SKCanvas canvas, TextPrimitive primitive)
    {
        using var paint = new SKPaint
        {
            Color = ToSkColor(primitive.FontColor),
            IsAntialias = true,
            TextSize = (float)Math.Max(1, primitive.FontSize),
            Typeface = SKTypeface.FromFamilyName(primitive.FontFamilyName)
        };

        canvas.DrawText(primitive.Text, (float)primitive.Position.X, (float)(primitive.Position.Y + paint.TextSize), paint);
    }

    private static SKPaint CreateStrokePaint(Avalonia.Media.Color color, double thickness)
    {
        return new SKPaint
        {
            Color = ToSkColor(color),
            Style = SKPaintStyle.Stroke,
            IsAntialias = true,
            StrokeWidth = (float)Math.Max(1, thickness),
            StrokeJoin = SKStrokeJoin.Round,
            StrokeCap = SKStrokeCap.Round
        };
    }

    private static SKPaint CreateFillPaint(Avalonia.Media.Color color)
    {
        return new SKPaint
        {
            Color = ToSkColor(color),
            Style = SKPaintStyle.Fill,
            IsAntialias = true
        };
    }

    private static SKColor ToSkColor(Avalonia.Media.Color color)
    {
        return new SKColor(color.R, color.G, color.B, color.A);
    }
}