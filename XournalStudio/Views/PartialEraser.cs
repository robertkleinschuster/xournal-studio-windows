using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace XournalStudio.Views;

/// <summary>
/// The partial eraser (user, 2026-11-16; Windows ink only erases whole strokes): erases exactly the circle shown at the
/// pen – strokes are cut by <see cref="StrokeEraser"/> and replaced by what is left of them, one undo step per drag.
/// The circle follows the pen while it hovers and erases; its outer edge is where erasing ends.
/// </summary>
public sealed class PartialEraser(UndoStack undo, Action<IReadOnlyCollection<InkStroke>> erased)
{
    private PageView? cursorPage;
    private Ellipse? cursor;
    private PageView? page;
    private PageEdit? edit;
    private Point last;

    /// <summary>Radius in page points.</summary>
    public double Radius { get; set; } = 4;

    /// <summary>Shows the circle at <paramref name="p"/> on <paramref name="view"/>.</summary>
    public void Hover(PageView view, Point p)
    {
        if (cursorPage != view)
        {
            Hide();
            cursor = new Ellipse { Stroke = new SolidColorBrush(Microsoft.UI.Colors.DimGray), StrokeThickness = 0.5, IsHitTestVisible = false };
            view.Overlay.Children.Add(cursor);
            cursorPage = view;
        }
        // The stroke lies inside the ellipse's bounds: its outer edge is the radius.
        Selection.Place(cursor!, new Rect(p.X - Radius, p.Y - Radius, 2 * Radius, 2 * Radius));
    }

    public void Hide()
    {
        if (cursor != null)
        {
            cursorPage!.Overlay.Children.Remove(cursor);
            cursor = null;
            cursorPage = null;
        }
    }

    public void Begin(PageView view, Point p)
    {
        page = view;
        edit = new PageEdit(view.Page);
        last = p;
        Hover(view, p);
        EraseAt(p);
    }

    /// <summary>Erases along the way from the last point (in steps of half the radius: nothing is skipped).</summary>
    public void Move(Point p)
    {
        if (page == null)
        {
            return;
        }
        double dx = p.X - last.X, dy = p.Y - last.Y;
        int steps = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(dx * dx + dy * dy) / (Radius / 2)));
        for (int i = 1; i <= steps; i++)
        {
            EraseAt(new Point(last.X + dx * i / steps, last.Y + dy * i / steps));
        }
        last = p;
        Hover(page, p);
    }

    /// <summary>The drag ended (or was canceled: what was erased stays erased): one undo step.</summary>
    public void End()
    {
        if (edit != null)
        {
            undo.Push(edit);
        }
        edit = null;
        page = null;
    }

    private void EraseAt(Point center)
    {
        var xopp = page!.Page;
        var ink = xopp.Ink!;
        var reach = new Rect(center.X - Radius, center.Y - Radius, 2 * Radius, 2 * Radius);
        var removed = new List<InkStroke>();
        foreach (var stroke in ink.GetStrokes().ToList())
        {
            var bounds = stroke.BoundingRect; // includes the stroke's width
            bounds.Intersect(reach);
            if (bounds.IsEmpty)
            {
                continue;
            }
            var attributes = stroke.DrawingAttributes;
            var transform = stroke.PointTransform;
            var samples = stroke.GetInkPoints().Select(p =>
            {
                var at = System.Numerics.Vector2.Transform(new((float)p.Position.X, (float)p.Position.Y), transform);
                return new InkSample(new Point(at.X, at.Y), p.Pressure);
            }).ToList();
            var pieces = StrokeEraser.Erase(samples, s => StrokeConverter.DrawnWidth(attributes, s.Pressure) / 2, center, Radius);
            if (pieces == null)
            {
                continue;
            }
            PageEdit.RemoveStrokes(xopp, [stroke]);
            removed.Add(stroke);
            if (!edit!.AddedStrokes.Remove(stroke))
            {
                edit.RemovedStrokes.Add(stroke); // was there before this drag
            }
            foreach (var piece in pieces)
            {
                var rest = StrokeConverter.FromSamples(piece, attributes);
                ink.AddStroke(rest);
                edit.AddedStrokes.Add(rest);
            }
        }
        if (removed.Count > 0)
        {
            erased(removed);
        }
    }
}
