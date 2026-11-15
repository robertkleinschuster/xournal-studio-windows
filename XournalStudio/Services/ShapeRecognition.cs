using System.Numerics;
using Windows.Foundation;
using Windows.UI.Input.Inking;
using Windows.UI.Input.Inking.Analysis;

namespace XournalStudio.Services;

/// <summary>
/// Shape recognition by Windows ink analysis (<see cref="InkAnalyzer"/>): a stroke Windows recognizes as a circle,
/// ellipse, triangle, rectangle or other polygon is replaced by a clean stroke along that shape.
/// </summary>
public static class ShapeRecognition
{
    private static readonly InkStrokeBuilder Builder = new();

    /// <summary>The clean shape for a drawn stroke, or null if Windows sees no shape in it.</summary>
    public static async Task<InkStroke?> RecognizeAsync(InkStroke stroke)
    {
        var analyzer = new InkAnalyzer();
        analyzer.AddDataForStroke(stroke);
        analyzer.SetStrokeDataKind(stroke.Id, InkAnalysisStrokeKind.Drawing);
        var result = await analyzer.AnalyzeAsync();
        if (result.Status != InkAnalysisStatus.Updated)
        {
            return null;
        }
        var drawing = analyzer.AnalysisRoot.FindNodes(InkAnalysisNodeKind.InkDrawing)
            .OfType<InkAnalysisInkDrawing>()
            .FirstOrDefault(d => d.DrawingKind != InkAnalysisDrawingKind.Drawing);
        if (drawing == null)
        {
            return null;
        }
        var outline = drawing.DrawingKind is InkAnalysisDrawingKind.Circle or InkAnalysisDrawingKind.Ellipse
            ? Ellipse(drawing.Center, drawing.Points)
            : drawing.Points.Append(drawing.Points[0]).ToList();
        return ToStroke(outline, stroke.DrawingAttributes);
    }

    // For circles and ellipses Windows reports the ends of both axes (0-2 and 1-3).
    private static List<Point> Ellipse(Point center, IReadOnlyList<Point> axes)
    {
        double rx = Distance(axes[0], axes[2]) / 2, ry = Distance(axes[1], axes[3]) / 2;
        double angle = Math.Atan2(axes[2].Y - axes[0].Y, axes[2].X - axes[0].X);
        double cos = Math.Cos(angle), sin = Math.Sin(angle);
        const int Steps = 72;
        return Enumerable.Range(0, Steps + 1).Select(i =>
        {
            double t = 2 * Math.PI * i / Steps;
            double x = rx * Math.Cos(t), y = ry * Math.Sin(t);
            return new Point(center.X + x * cos - y * sin, center.Y + x * sin + y * cos);
        }).ToList();
    }

    private static InkStroke ToStroke(IReadOnlyList<Point> points, InkDrawingAttributes pen)
    {
        var attributes = new InkDrawingAttributes
        {
            Color = pen.Color,
            Size = pen.Size,
            PenTip = PenTipShape.Circle,
            FitToCurve = false,
            IgnorePressure = true, // even width, like Xournal's shapes
        };
        Builder.SetDefaultDrawingAttributes(attributes);
        var stroke = Builder.CreateStrokeFromInkPoints(points.Select(p => new InkPoint(p, 0.5f)), Matrix3x2.Identity);
        stroke.DrawingAttributes = attributes;
        return stroke;
    }

    private static double Distance(Point a, Point b) => Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}
