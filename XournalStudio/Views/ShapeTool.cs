using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;

namespace XournalStudio.Views;

/// <summary>The shape tool: line, arrow, rectangle or ellipse dragged out, previewed, then added as one stroke.</summary>
public sealed class ShapeTool(UndoStack undo, Func<InkDrawingAttributes> pen)
{
    private PageView? view;
    private Point start;
    private Polyline? preview;

    public ShapeKind Shape { get; set; }

    public void Begin(PageView page, Point p)
    {
        view = page;
        start = p;
        var attributes = pen();
        preview = new Polyline
        {
            Stroke = new SolidColorBrush(attributes.Color),
            StrokeThickness = attributes.Size.Width,
            StrokeLineJoin = PenLineJoin.Round,
            StrokeStartLineCap = PenLineCap.Round,
            StrokeEndLineCap = PenLineCap.Round,
        };
        page.Overlay.Children.Add(preview);
    }

    public void Move(Point p)
    {
        var points = new PointCollection();
        foreach (var point in ShapeGeometry.Outline(Shape, start, p, pen().Size.Width))
        {
            points.Add(point);
        }
        preview!.Points = points;
    }

    public void End(Point end)
    {
        var page = view!.Page;
        Cancel();
        if (Math.Abs(end.X - start.X) + Math.Abs(end.Y - start.Y) < 2)
        {
            return; // a tap, not a shape
        }
        var attributes = pen();
        var stroke = ShapeGeometry.CreateStroke(ShapeGeometry.Outline(Shape, start, end, attributes.Size.Width), attributes);
        page.Ink!.AddStroke(stroke);
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
    }
}
