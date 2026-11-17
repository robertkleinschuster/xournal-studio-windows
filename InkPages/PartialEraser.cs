using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

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
    private readonly TranslateTransform cursorAt = new();
    private InkPage? page;
    private PageEdit? edit;
    private Point last;

    // The strokes of the drag's page read so far (by ink id), and the pieces it made: each read from the ink once per
    // drag, not on every pointer event.
    private readonly Dictionary<uint, SampledStroke> sampled = new();

    private static readonly Windows.UI.Color CursorColor = Microsoft.UI.Colors.DimGray;

    /// <summary>Radius in page points.</summary>
    public double Radius { get; set; } = 4;

    /// <summary>Shows the circle at <paramref name="p"/> on <paramref name="view"/>.</summary>
    public void Hover(InkPage view, Point p)
    {
        if (cursorPage != view)
        {
            Hide();
            // On the sheet, not the window: a gray that shows on white paper in either theme.
            cursor = new Ellipse
            {
                Stroke = new SolidColorBrush(CursorColor),
                StrokeThickness = 0.5,
                IsHitTestVisible = false,
                RenderTransform = cursorAt, // moved per pen event without a layout pass
            };
            view.Overlay.Children.Add(cursor);
            cursorPage = view;
        }
        // The stroke lies inside the ellipse's bounds: its outer edge is the radius.
        if (cursor!.Width != 2 * Radius)
        {
            cursor.Width = cursor.Height = 2 * Radius;
        }
        cursorAt.X = p.X - Radius;
        cursorAt.Y = p.Y - Radius;
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

    /// <summary>Other pages are shown: the circle goes and no old page is kept.</summary>
    public void ForgetPages()
    {
        Hide();
        page = null;
        edit = null;
        sampled.Clear();
    }

    public void Begin(InkPage view, Point p)
    {
        page = view;
        edit = new PageEdit(view);
        last = p;
        sampled.Clear();
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
        sampled.Clear();
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
            uint id = stroke.Id;
            if (!sampled.TryGetValue(id, out var read))
            {
                read = SampledStroke.Of(stroke);
                sampled[id] = read;
            }
            var (attributes, halfWidth) = (read.Attributes, read.HalfWidth);
            List<List<InkSample>> pieces = [read.Samples];
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
            sampled.Remove(stroke.Id);
            foreach (var piece in pieces)
            {
                var rest = InkStrokes.FromSamples(piece, attributes);
                ink.AddStroke(rest);
                edit.AddedStrokes.Add(rest);
                sampled[rest.Id] = SampledStroke.Made(piece, attributes); // what reading it would give
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
