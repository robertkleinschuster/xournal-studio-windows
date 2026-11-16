using System.Numerics;
using System.Xml.Linq;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace XournalStudio.Views;

/// <summary>
/// The selection tool (also the pen's barrel button): a lasso or a rectangle selects strokes (Windows ink's
/// <c>SelectWithPolyLine</c>) and pictures inside it, a tap selects the picture under the pen; dragging inside the
/// selection moves it, dragging a corner handle scales it. A rectangle on a PDF page also marks that area for copying.
/// </summary>
public sealed class SelectionTool(UndoStack undo)
{
    private const double MinScale = 0.05;

    private enum Gesture { None, Lasso, Rectangle, Move, Scale }

    private Gesture gesture;
    private PageView? view;
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
    /// (XoppPage.BeginPreview), so a snapshot is consistent at any moment.
    /// </summary>
    public bool IsDragging => gesture is Gesture.Move or Gesture.Scale;

    /// <summary>A selection was made, changed (moved, scaled) or cleared.</summary>
    public event EventHandler? SelectionChanged;

    /// <param name="mode">Lasso or rectangle for a new selection (the barrel button always lassos).</param>
    /// <summary>A press at <paramref name="p"/> would move (inside) or scale (corner) the selection.</summary>
    public bool Grabs(PageView page, Point p) =>
        Current?.View == page && Current.HasContent && (Current.HandleAt(p) >= 0 || Current.Contains(p));

    public void Begin(PageView page, Point p, SelectMode mode)
    {
        view = page;
        start = p;
        moved = false;
        gesture = Gesture.None;
        if (Current?.View == page && Current.HasContent)
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
        }
        if (gesture == Gesture.None)
        {
            Clear();
            if (mode == SelectMode.Lasso)
            {
                lasso = new Polyline { Stroke = Selection.AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
                lasso.Points.Add(p);
                page.Overlay.Children.Add(lasso);
                gesture = Gesture.Lasso;
            }
            else
            {
                rectangle = new Rectangle { Stroke = Selection.AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
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
                Selection.Place(rectangle!, new Rect(start, p));
                break;
            case Gesture.Move:
                Current?.Preview(Matrix3x2.CreateTranslation((float)(p.X - start.X), (float)(p.Y - start.Y)));
                break;
            case Gesture.Scale:
                Current?.Preview(Matrix3x2.CreateScale((float)ScaleFactor(p), new Vector2((float)anchor.X, (float)anchor.Y)));
                break;
        }
    }

    public void End(Point p)
    {
        var page = view!;
        var finished = gesture;
        var points = lasso?.Points.ToList();
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
                r.Intersect(new Rect(0, 0, page.Page.Width, page.Page.Height));
                var area = page.Pdf != null && !r.IsEmpty && r.Width >= 4 && r.Height >= 4 ? r : (Rect?)null;
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
                Current.Commit(undo);
                SelectionChanged?.Invoke(this, EventArgs.Empty);
                break;
        }
    }

    public void Cancel()
    {
        if (gesture is Gesture.Move or Gesture.Scale)
        {
            Current?.Revert();
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
        RemoveFeedback();
        gesture = Gesture.None;
    }

    /// <summary>Selects these strokes and pictures (pasted, duplicated, all of a page).</summary>
    public void Select(PageView page, List<InkStroke> strokes, List<XElement> images)
    {
        Clear();
        if (strokes.Count + images.Count > 0)
        {
            Current = new Selection(page, strokes, images, null);
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>
    /// No selection. In the middle of a drag the drag is reverted – commands wait for it instead
    /// (<see cref="EditCommands.RefusedWhileDragging"/>); a new selection by the pen ends it this way.
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
        }
        Current.Remove();
        Current = null;
        SelectionChanged?.Invoke(this, EventArgs.Empty);
    }

    // Strokes: Windows ink selects those inside the polygon. Pictures: those with all corners inside.
    private void SelectInside(PageView page, List<Point> polygon, Rect? area)
    {
        if (polygon.Count < 3)
        {
            return;
        }
        Clear();
        var ink = page.Page.Ink!;
        ink.SelectWithPolyLine(polygon);
        var strokes = ink.GetStrokes().Where(s => s.Selected).ToList();
        strokes.ForEach(s => s.Selected = false); // the selection keeps its own list
        var images = page.Page.Images.Where(i => Corners(XoppImage.GetBounds(i)).All(c => Inside(c, polygon))).ToList();
        if (strokes.Count + images.Count > 0 || area != null)
        {
            Clear(); // one may have been made by keyboard during the drag (Ctrl+V, Ctrl+A)
            Current = new Selection(page, strokes, images, area);
            SelectionChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private void SelectPictureAt(PageView page, Point p)
    {
        var picture = page.Page.Images.LastOrDefault(i => XoppImage.GetBounds(i).Contains(p)); // the topmost
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
