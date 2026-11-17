using Windows.UI.Input.Inking;
using Windows.UI.Input.Inking.Analysis;

namespace InkPages;

/// <summary>
/// Shape recognition by Windows ink analysis (<see cref="InkAnalyzer"/>): a stroke Windows recognizes as a circle,
/// ellipse, triangle, rectangle or other polygon is replaced by a clean stroke along that shape.
/// </summary>
public static class ShapeRecognition
{
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
            ? ShapeGeometry.EllipseFromAxes(drawing.Center, drawing.Points)
            : drawing.Points.Append(drawing.Points[0]).ToList();
        return ShapeGeometry.CreateStroke(outline, stroke.DrawingAttributes);
    }
}
