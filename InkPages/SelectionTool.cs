using System.Numerics;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using Canvas = Microsoft.UI.Xaml.Controls.Canvas;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace InkPages;

/// <summary>
/// The selection tool (also the pen's barrel button): a lasso or a rectangle selects strokes (Windows ink's
/// <c>SelectWithPolyLine</c>) and pictures inside it, a tap selects the picture under the pen; dragging inside the
/// selection moves it, dragging a corner handle scales it. A rectangle on a page that keeps selected areas also keeps that area (<see cref="InkPage.KeepsSelectedAreas"/>).
/// </summary>
public sealed class SelectionTool(UndoStack undo)
{
    private const double MinScale = 0.05;

    private enum Gesture { None, Lasso, Rectangle, Move, Scale }

    private Gesture gesture;
    private InkPage? view;
    private Point start;
    private Point anchor, handle; // scaling: the fixed corner and where the pen pressed
    private bool moved;           // the pen left the start point (a closed lasso ends where it began: no tap)
    private Polyline? lasso;
    private Rectangle? rectangle;

    public SelectMode Mode { get; set; }

    public Selection? Current { get; private set; }

    /// <summary>
    /// The selection is being moved or scaled (pen down on it, also before the first move): commands that read or
    /// change content wait for it to be put down (edit commands, undo/redo), so a move is never lost or measured mid-way.
    /// Saving, recovery copies and export never wait: the page writes a previewed stroke with its committed transform
    /// (InkPage.BeginStrokePreview), so a snapshot is consistent at any moment.
    /// </summary>
    public bool IsDragging => gesture is Gesture.Move or Gesture.Scale;

    /// <summary>A selection was made, changed (moved, scaled) or cleared.</summary>
    public event EventHandler? SelectionChanged;

    /// <summary>
    /// Another page under a point of <c>page</c> (page points), with the offset from <c>page</c>'s coordinates to its
    /// own; null over the page itself or between pages (set by the pages' owner).
    /// </summary>
    public Func<InkPage, Point, (InkPage Page, Vector2 Offset)?> OtherPageAt { get; set; } = (_, _) => null;

    /// <summary>
    /// Moves a selection's content to another page with this transform (from its place before the drag into the target
    /// page's coordinates); returns what is there now and the edit (not recorded), null if it stayed (set by the owner).
    /// </summary>
    public Func<Selection, InkPage, Matrix3x2, (List<InkStroke> Strokes, List<object> Pictures, IPageEdit Edit)?> TransferToPage
    { get; set; } = (_, _, _) => null;

    /// <summary>A dragged selection went on to another page: the drag continues there (the pen in its coordinates).</summary>
    public event Action<InkPage, Point>? DragChangedPage;

    // The steps of the running drag that are already made (hand-overs to other pages); recorded as one undo step with
    // the final move when the drag ends.
    private readonly List<IPageEdit> dragEdits = new();
    private bool handOverFailed; // once per drag: a failed hand-over is said once, the content stays on its page

    /// <summary>A press at <paramref name="p"/> would move (inside) or scale (corner) the selection.</summary>
    public bool Grabs(InkPage page, Point p) =>
        Current?.Page == page && Current.HasContent && (Current.HandleAt(p) >= 0 || Current.Contains(p));

    /// <param name="mode">Lasso or rectangle for a new selection (the barrel button always lassos).</param>
    public void Begin(InkPage page, Point p, SelectMode mode)
    {
        view = page;
        start = p;
        moved = false;
        gesture = Gesture.None;
        dragEdits.Clear();
        handOverFailed = false;
        if (Current?.Page == page && Current.HasContent)
        {
            int corner = Current.HandleAt(p);
            if (corner >= 0)
            {
                gesture = Gesture.Scale;
                handle = p; // relative to where the pen pressed (the handle reaches beyond the corner)
                anchor = Current.CornerOf((corner + 2) % 4);
            }
            else if (Current.Contains(p))
            {
                gesture = Gesture.Move;
            }
            if (gesture != Gesture.None)
            {
                // The dragged pictures (XAML) are drawn above the neighbouring pages until the drag ends.
                Canvas.SetZIndex(page, 1);
            }
        }
        if (gesture == Gesture.None)
        {
            Clear();
            if (mode == SelectMode.Lasso)
            {
                lasso = Themed.AccentOutline<Polyline>("Polyline");
                lasso.Points.Add(p);
                page.Overlay.Children.Add(lasso);
                gesture = Gesture.Lasso;
            }
            else
            {
                rectangle = Themed.AccentOutline<Rectangle>("Rectangle");
                page.Overlay.Children.Add(rectangle);
                gesture = Gesture.Rectangle;
            }
        }
    }

    public void Move(Point p)
    {
        moved |= Math.Abs(p.X - start.X) + Math.Abs(p.Y - start.Y) >= 3;
        switch (gesture)
        {
            case Gesture.Lasso:
                lasso!.Points.Add(p);
                break;
            case Gesture.Rectangle:
                Overlay.Place(rectangle!, new Rect(start, p));
                break;
            case Gesture.Move:
                Current?.Preview(Matrix3x2.CreateTranslation((float)(p.X - start.X), (float)(p.Y - start.Y)));
                HandOverIfOverOtherPage(p);
                break;
            case Gesture.Scale:
                Current?.Preview(Matrix3x2.CreateScale((float)ScaleFactor(p), new Vector2((float)anchor.X, (float)anchor.Y)));
                break;
        }
    }

    public void End(Point p)
    {
        if (gesture == Gesture.Move)
        {
            HandOverIfOverOtherPage(p); // released over another page without a move there first
            p = lastOnPage ?? p;
        }
        var page = view!;
        var finished = gesture;
        var points = lasso?.Points.ToList();
        Canvas.SetZIndex(page, 0);
        RemoveFeedback();
        gesture = Gesture.None;
        bool tap = !moved && Math.Abs(p.X - start.X) + Math.Abs(p.Y - start.Y) < 3;
        switch (finished)
        {
            case Gesture.Lasso when !tap:
                SelectInside(page, points!, null);
                break;
            case Gesture.Rectangle when !tap:
                var r = new Rect(start, p);
                r.Intersect(new Rect(0, 0, page.Width, page.Height));
                var area = page.KeepsSelectedAreas && !r.IsEmpty && r.Width >= 4 && r.Height >= 4 ? r : (Rect?)null;
                SelectInside(page, [.. Corners(r)], area);
                break;
            case Gesture.Lasso or Gesture.Rectangle:
                SelectPictureAt(page, p);
                break;
            case Gesture.Move or Gesture.Scale when tap:
                // A tap on the selection clears it (or selects the picture under the pen).
                Current?.Revert();
                Clear();
                SelectPictureAt(page, p);
                break;
            case Gesture.Move or Gesture.Scale when Current != null:
                RecordDrag(Current.Commit());
                SelectionChanged?.Invoke(this, EventArgs.Empty);
                break;
        }
        RecordDrag(null); // hand-overs of a drag that ended otherwise (they are made)
    }

    // The pen's last place, in the page's coordinates, after a hand-over during End (null: no hand-over).
    private Point? lastOnPage;

    /// <summary>
    /// The pen of a moved selection is over another page: the content goes there now (an app's file keeps every
    /// element on one page; each page's ink is drawn only within the page, so content dragged beyond it would be cut off), and
    /// the drag goes on from there.
    /// </summary>
    private void HandOverIfOverOtherPage(Point p)
    {
        lastOnPage = null;
        // Over its own page nothing changes; only beyond it the other pages are looked up (each pen event comes here).
        if (handOverFailed || Current is not { } from || OnPage(view!, p) || OtherPageAt(view!, p) is not { } target)
        {
            return;
        }
        if (TransferToPage(from, target.Page, from.Live * Matrix3x2.CreateTranslation(target.Offset)) is not { } moved)
        {
            handOverFailed = true; // the user was told; the drag goes on with the content on its page
            return;
        }
        dragEdits.Add(moved.Edit);
        from.Remove();
        Canvas.SetZIndex(view!, 0);
        view = target.Page;
        Canvas.SetZIndex(view, 1);
        var q = new Point(p.X + target.Offset.X, p.Y + target.Offset.Y);
        start = q; // the content is where it was shown: the move goes on from here
        lastOnPage = q;
        Current = new Selection(view, moved.Strokes, moved.Pictures, null);
        SelectionChanged?.Invoke(this, EventArgs.Empty);
        DragChangedPage?.Invoke(view, q);
    }

    private static bool OnPage(InkPage page, Point p) =>
        p.X >= 0 && p.Y >= 0 && p.X <= page.Width && p.Y <= page.Height;

    // Records the drag as one undo step: its hand-overs and its final move (if any).
    private void RecordDrag(IPageEdit? last)
    {
        if (last != null)
        {
            dragEdits.Add(last);
        }
        if (dragEdits.Count == 1)
        {
            undo.Push(dragEdits[0]);
        }
        else if (dragEdits.Count > 1)
        {
            undo.Push(new CombinedEdit(dragEdits.ToList()));
        }
        dragEdits.Clear();
    }

    public void Cancel()
    {
        if (view != null)
        {
            Canvas.SetZIndex(view, 0);
        }
        if (gesture is Gesture.Move or Gesture.Scale)
        {
            Current?.Revert();
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
        RecordDrag(null); // hand-overs already made stay (and can be undone)
        RemoveFeedback();
        gesture = Gesture.None;
    }

    /// <summary>Selects these strokes and pictures (pasted, duplicated, all of a page).</summary>
    public void Select(InkPage page, List<InkStroke> strokes, List<object> pictures)
    {
        Clear();
        if (strokes.Count + pictures.Count > 0)
        {
            Current = new Selection(page, strokes, pictures, null);
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>
    /// No selection. In the middle of a drag the drag is reverted – commands wait for it instead
    /// (see <see cref="IsDragging"/>); a new selection by the pen ends it this way.
    /// </summary>
    public void Clear()
    {
        if (Current == null)
        {
            return;
        }
        if (gesture is Gesture.Move or Gesture.Scale)
        {
            Current.Revert();
            gesture = Gesture.None;
            RecordDrag(null); // hand-overs already made stay (and can be undone)
        }
        Current.Remove();
        Current = null;
        SelectionChanged?.Invoke(this, EventArgs.Empty);
    }

    // Strokes: Windows ink selects those inside the polygon. Pictures: those with all corners inside.
    private void SelectInside(InkPage page, List<Point> polygon, Rect? area)
    {
        if (polygon.Count < 3)
        {
            return;
        }
        Clear();
        var ink = page.Ink.StrokeContainer;
        ink.SelectWithPolyLine(polygon);
        var strokes = ink.GetStrokes().Where(s => s.Selected).ToList();
        strokes.ForEach(s => s.Selected = false); // the selection keeps its own list
        var pictures = page.Pictures.Where(i => Corners(page.PictureBounds(i)).All(c => Inside(c, polygon))).ToList();
        if (strokes.Count + pictures.Count > 0 || area != null)
        {
            Clear(); // one may have been made by keyboard during the drag (Ctrl+V, Ctrl+A)
            Current = new Selection(page, strokes, pictures, area);
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private void SelectPictureAt(InkPage page, Point p)
    {
        var picture = page.Pictures.LastOrDefault(i => page.PictureBounds(i).Contains(p)); // the topmost
        if (picture != null)
        {
            Select(page, [], [picture]);
        }
    }

    // Uniform: the pen's movement along the line from the fixed corner to where it pressed (pictures keep their aspect ratio, ink its look).
    private double ScaleFactor(Point p)
    {
        double dx = handle.X - anchor.X, dy = handle.Y - anchor.Y;
        double length2 = dx * dx + dy * dy;
        if (length2 < 1)
        {
            return 1;
        }
        return Math.Max(MinScale, ((p.X - anchor.X) * dx + (p.Y - anchor.Y) * dy) / length2);
    }

    private void RemoveFeedback()
    {
        if (lasso != null)
        {
            view?.Overlay.Children.Remove(lasso);
            lasso = null;
        }
        if (rectangle != null)
        {
            view?.Overlay.Children.Remove(rectangle);
            rectangle = null;
        }
    }

    private static Point[] Corners(Rect r) =>
        [new(r.Left, r.Top), new(r.Right, r.Top), new(r.Right, r.Bottom), new(r.Left, r.Bottom)];

    // Even-odd rule (ray casting), the same rule the lasso of Windows ink uses for its polygon.
    private static bool Inside(Point p, IReadOnlyList<Point> polygon)
    {
        bool inside = false;
        for (int i = 0, j = polygon.Count - 1; i < polygon.Count; j = i++)
        {
            var a = polygon[i];
            var b = polygon[j];
            if ((a.Y > p.Y) != (b.Y > p.Y) && p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X)
            {
                inside = !inside;
            }
        }
        return inside;
    }
}
