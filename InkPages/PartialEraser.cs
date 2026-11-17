using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace InkPages;

/// <summary>
/// The partial eraser (user, 2026-11-16; Windows ink only erases whole strokes): erases exactly the circle shown at the
/// pen – strokes are cut by <see cref="StrokeEraser"/> and replaced by what is left of them, one undo step per drag.
/// The circle follows the pen while it hovers and erases; its outer edge is where erasing ends.
/// </summary>
public sealed class PartialEraser(UndoStack undo, Action<IReadOnlyCollection<InkStroke>> erased)
{
    private InkPage? cursorPage;
    private Ellipse? cursor;
    private InkPage? page;
    private PageEdit? edit;
    private Point last;

    /// <summary>Radius in page points.</summary>
    public double Radius { get; set; } = 4;

    /// <summary>Shows the circle at <paramref name="p"/> on <paramref name="view"/>.</summary>
    public void Hover(InkPage view, Point p)
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

    public void Begin(InkPage view, Point p)
    {
        page = view;
        edit = new PageEdit(view);
        last = p;
        Hover(view, p);
        EraseAt([p]);
    }

    /// <summary>
    /// Erases along the way from the last point through <paramref name="path"/> (one pointer event's samples, oldest
    /// first; circles every half radius: nothing is skipped) in one pass over the page's strokes.
    /// </summary>
    public void MoveAlong(IReadOnlyList<Point> path)
    {
        if (page == null || path.Count == 0)
        {
            return;
        }
        var centers = new List<Point>();
        foreach (var p in path)
        {
            double dx = p.X - last.X, dy = p.Y - last.Y;
            int steps = Math.Max(1, (int)Math.Ceiling(Math.Sqrt(dx * dx + dy * dy) / (Radius / 2)));
            for (int i = 1; i <= steps; i++)
            {
                centers.Add(new Point(last.X + dx * i / steps, last.Y + dy * i / steps));
            }
            last = p;
        }
        EraseAt(centers);
        Hover(page, last);
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

    /// <summary>Applies one pointer event's swept eraser circles in a single pass over the page's strokes.</summary>
    private void EraseAt(IReadOnlyList<Point> centers)
    {
        if (centers.Count == 0)
        {
            return;
        }
        var ink = page!.Ink.StrokeContainer;
        double left = centers.Min(p => p.X) - Radius, right = centers.Max(p => p.X) + Radius;
        double top = centers.Min(p => p.Y) - Radius, bottom = centers.Max(p => p.Y) + Radius;
        var sweepBounds = new Rect(left, top, right - left, bottom - top);
        var removed = new List<(InkStroke Stroke, List<List<InkSample>> Pieces,
            Windows.UI.Input.Inking.InkDrawingAttributes Attributes)>();
        foreach (var stroke in ink.GetStrokes())
        {
            var bounds = stroke.BoundingRect; // includes the stroke's width
            bounds.Intersect(sweepBounds);
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
            var halfWidth = (InkSample s) => InkStrokes.DrawnWidth(attributes, s.Pressure) / 2;
            List<List<InkSample>> pieces = [samples];
            bool touched = false;
            foreach (var center in centers)
            {
                var next = new List<List<InkSample>>();
                foreach (var piece in pieces)
                {
                    var cut = StrokeEraser.Erase(piece, halfWidth, center, Radius);
                    if (cut == null)
                    {
                        next.Add(piece);
                    }
                    else
                    {
                        touched = true;
                        next.AddRange(cut);
                    }
                }
                pieces = next;
                if (pieces.Count == 0)
                {
                    break;
                }
            }
            if (touched)
            {
                removed.Add((stroke, pieces, attributes));
            }
        }
        foreach (var (stroke, pieces, attributes) in removed)
        {
            if (!edit!.AddedStrokes.Remove(stroke))
            {
                edit.RemovedStrokes.Add(stroke); // was there before this drag
            }
            foreach (var piece in pieces)
            {
                var rest = InkStrokes.FromSamples(piece, attributes);
                ink.AddStroke(rest);
                edit.AddedStrokes.Add(rest);
            }
        }
        if (removed.Count != 0)
        {
            // The touched strokes come from the ink itself (no identity lookup); no other stroke is ever left selected.
            foreach (var (stroke, _, _) in removed)
            {
                stroke.Selected = true;
            }
            ink.DeleteSelected();
            erased(removed.Select(r => r.Stroke).ToList());
        }
    }
}
