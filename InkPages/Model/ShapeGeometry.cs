using System.Numerics;
using Windows.Foundation;
using Windows.UI.Input.Inking;

namespace InkPages;

public enum ShapeKind
{
    Line,
    Arrow,
    Rectangle,
    Ellipse,
}

/// <summary>
/// Outlines of the shape tools and of recognized shapes, and the ink strokes made from them. A shape is one
/// polyline stroke of even width (as note apps such as Xournal++ store their shapes).
/// </summary>
public static class ShapeGeometry
{
    private const int EllipseSteps = 72;

    /// <summary>The outline of a shape dragged from <paramref name="start"/> to <paramref name="end"/>.</summary>
    public static IReadOnlyList<Point> Outline(ShapeKind kind, Point start, Point end, double penWidth) => kind switch
    {
        ShapeKind.Line => [start, end],
        ShapeKind.Arrow => Arrow(start, end, penWidth),
        ShapeKind.Rectangle =>
            [start, new Point(end.X, start.Y), end, new Point(start.X, end.Y), start],
        ShapeKind.Ellipse => Ellipse(
            new Point((start.X + end.X) / 2, (start.Y + end.Y) / 2),
            Math.Abs(end.X - start.X) / 2, Math.Abs(end.Y - start.Y) / 2, 0),
        _ => throw new ArgumentOutOfRangeException(nameof(kind)),
    };

    /// <summary>
    /// An ellipse from the ends of both axes (0–2 and 1–3), as Windows ink analysis reports circles and ellipses.
    /// </summary>
    public static IReadOnlyList<Point> EllipseFromAxes(Point center, IReadOnlyList<Point> axes)
    {
        double rx = Distance(axes[0], axes[2]) / 2, ry = Distance(axes[1], axes[3]) / 2;
        double angle = Math.Atan2(axes[2].Y - axes[0].Y, axes[2].X - axes[0].X);
        return Ellipse(center, rx, ry, angle);
    }

    /// <summary>A stroke along <paramref name="points"/> in the pen's color and width, without pressure.</summary>
    public static InkStroke CreateStroke(IReadOnlyList<Point> points, InkDrawingAttributes pen)
    {
        var attributes = new InkDrawingAttributes
        {
            Color = pen.Color,
            Size = pen.Size,
            PenTip = PenTipShape.Circle,
            FitToCurve = false,
            IgnorePressure = true,
        };
        return InkStrokes.FromSamples(points.Select(p => new InkSample(p, 0.5f)), attributes);
    }

    // Shaft, then both sides of the head, drawn in one go: start → end → side → end → other side.
    private static List<Point> Arrow(Point start, Point end, double penWidth)
    {
        double length = Distance(start, end);
        if (length == 0)
        {
            return [start, end];
        }
        double head = Math.Min(Math.Max(8, 4 * penWidth), length * 0.4);
        double angle = Math.Atan2(end.Y - start.Y, end.X - start.X);
        const double Spread = 25 * Math.PI / 180;
        Point Side(double a) => new(end.X - head * Math.Cos(a), end.Y - head * Math.Sin(a));
        return [start, end, Side(angle - Spread), end, Side(angle + Spread)];
    }

    private static List<Point> Ellipse(Point center, double rx, double ry, double angle)
    {
        double cos = Math.Cos(angle), sin = Math.Sin(angle);
        return Enumerable.Range(0, EllipseSteps + 1).Select(i =>
        {
            double t = 2 * Math.PI * i / EllipseSteps;
            double x = rx * Math.Cos(t), y = ry * Math.Sin(t);
            return new Point(center.X + x * cos - y * sin, center.Y + x * sin + y * cos);
        }).ToList();
    }

    private static double Distance(Point a, Point b) => Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}
