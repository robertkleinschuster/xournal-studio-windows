using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;

namespace InkPages;

/// <summary>The shape tool: line, arrow, rectangle or ellipse dragged out, previewed, then added as one stroke.</summary>
public sealed class ShapeTool(UndoStack undo, Func<InkDrawingAttributes> pen)
{
    private InkPage? view;
    private Point start;
    private Polyline? preview;
    private double width; // the pen's size, read once per shape

    public ShapeKind Shape { get; set; }

    public void Begin(InkPage page, Point p)
    {
        view = page;
        start = p;
        var attributes = pen();
        width = InkStrokes.ToolWidth(attributes);
        preview = new Polyline
        {
            Stroke = new SolidColorBrush(attributes.Color),
            StrokeThickness = width,
            StrokeLineJoin = PenLineJoin.Round,
            StrokeStartLineCap = PenLineCap.Round,
            StrokeEndLineCap = PenLineCap.Round,
        };
        page.Overlay.Children.Add(preview);
    }

    public void Move(Point p)
    {
        var points = new PointCollection();
        foreach (var point in ShapeGeometry.Outline(Shape, start, p, width))
        {
            points.Add(point);
        }
        preview!.Points = points;
    }

    public void End(Point end)
    {
        var page = view!;
        Cancel();
        if (Math.Abs(end.X - start.X) + Math.Abs(end.Y - start.Y) < 2)
        {
            return; // a tap, not a shape
        }
        var attributes = pen();
        var stroke = ShapeGeometry.CreateStroke(ShapeGeometry.Outline(Shape, start, end, InkStrokes.ToolWidth(attributes)), attributes);
        page.Ink.StrokeContainer.AddStroke(stroke);
        var edit = new PageEdit(page);
        edit.AddedStrokes.Add(stroke);
        undo.Push(edit);
    }

    public void Cancel()
    {
        if (preview != null)
        {
            view?.Overlay.Children.Remove(preview);
            preview = null;
        }
        view = null; // no page kept after the shape (another document may replace it)
    }
}
