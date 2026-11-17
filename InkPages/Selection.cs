using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace InkPages;

/// <summary>
/// Selected strokes and pictures of one page – after selecting, pasting or duplicating – with a dashed frame and
/// corner handles on the page's overlay. Moving and scaling preview live (strokes by their <c>PointTransform</c>,
/// which Windows ink redraws; pictures by a render transform) and are written to the page when the drag ends.
/// A rectangle on a PDF page also keeps that <see cref="Area"/>, so copying takes what is seen there.
/// </summary>
public sealed class Selection
{
    private const double Margin = 4;      // frame distance around the content
    private const double HandleSize = 8;
    private const double HandleReach = 14; // pen-friendly hit distance of a handle

    private readonly Rectangle frame;
    private readonly Rectangle[] handles;
    // The strokes' transforms when the drag began (null: no drag); the page gets them too, for snapshots
    // (InkPage.BeginStrokePreview).
    private Dictionary<InkStroke, Matrix3x2>? starts;
    private Action<Matrix3x2>? picturePreview; // started once per drag
    // A move slides the frame and handles by a render transform (no layout pass); a scale places them anew.
    private readonly TranslateTransform slide = new();
    private Matrix3x2 live = Matrix3x2.Identity;

    public Selection(InkPage page, List<InkStroke> strokes, List<object> pictures, Rect? area)
    {
        Page = page;
        // Each stroke and picture once: its element is moved once. (A selection's strokes are of one page's ink, where ids
        // are unique.)
        Strokes = strokes.DistinctBy(s => s.Id).ToList();
        Pictures = pictures.Distinct().ToList();
        Area = area;
        Bounds = area ?? ContentBounds();
        frame = new Rectangle { Stroke = AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2], RenderTransform = slide };
        page.Overlay.Children.Add(frame);
        handles = HasContent
            ? Enumerable.Range(0, 4).Select(_ => new Rectangle { Width = HandleSize, Height = HandleSize, Fill = AccentBrush, RenderTransform = slide }).ToArray()
            : [];
        foreach (var handle in handles)
        {
            page.Overlay.Children.Add(handle);
        }
        PlaceFeedback(Bounds);
    }

    public InkPage Page { get; }

    public List<InkStroke> Strokes { get; }

    /// <summary>The app's pictures (see <see cref="InkPage.Pictures"/>).</summary>
    public List<object> Pictures { get; }

    /// <summary>The rectangle drawn on a PDF page (copying renders it); null once the content was moved.</summary>
    public Rect? Area { get; private set; }

    /// <summary>The area, otherwise what the strokes and pictures cover (page points).</summary>
    public Rect Bounds { get; private set; }

    public bool HasContent => Strokes.Count + Pictures.Count > 0;

    public static Brush AccentBrush => (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];

    /// <summary>The corner handle at <paramref name="p"/> (0 top-left, clockwise), or -1.</summary>
    public int HandleAt(Point p)
    {
        if (Bounds.Contains(p))
        {
            return -1; // on the content it moves (small selections would otherwise be handles only)
        }
        for (int i = 0; i < handles.Length; i++)
        {
            var c = Corner(Inflate(Bounds, Margin), i);
            if (Math.Abs(c.X - p.X) <= HandleReach && Math.Abs(c.Y - p.Y) <= HandleReach)
            {
                return i;
            }
        }
        return -1;
    }

    public bool Contains(Point p) => Inflate(Bounds, Margin).Contains(p);

    /// <summary>A corner of the content (0 top-left, clockwise).</summary>
    public Point CornerOf(int index) => Corner(Bounds, index);

    /// <summary>The previewed transform (identity while not dragged).</summary>
    public Matrix3x2 Live => live;

    /// <summary>Shows the content transformed by <paramref name="m"/> (relative to where it was when the drag began).</summary>
    public void Preview(Matrix3x2 m)
    {
        if (starts != null && m == live)
        {
            return; // nothing moved (the pen held still, the view at its end): no stroke or picture to touch
        }
        if (starts == null)
        {
            starts = new Dictionary<InkStroke, Matrix3x2>();
            foreach (var stroke in Strokes)
            {
                starts[stroke] = stroke.PointTransform; // (a stroke listed twice is taken once)
                Page.BeginStrokePreview(stroke, stroke.PointTransform);
            }
        }
        foreach (var (stroke, start) in starts)
        {
            stroke.PointTransform = start * m;
        }
        if (Pictures.Count > 0)
        {
            picturePreview ??= Page.PreviewPictures(Pictures);
            picturePreview(m);
        }
        live = m;
        if (m.M11 == 1 && m.M22 == 1 && m.M12 == 0 && m.M21 == 0)
        {
            if (slide.X == 0 && slide.Y == 0)
            {
                PlaceFeedback(Bounds); // the frame at the content's place; the move slides it
            }
            slide.X = m.M31;
            slide.Y = m.M32;
        }
        else
        {
            slide.X = slide.Y = 0;
            PlaceFeedback(TransformBounds(Bounds, m));
        }
    }

    /// <summary>Writes the previewed transform to the page; returns its undo step (null: nothing changed).</summary>
    public IPageEdit? Commit()
    {
        var m = live;
        live = Matrix3x2.Identity;
        if (m.IsIdentity)
        {
            EndPreview();
            return null;
        }
        // A transform other than identity comes only from Preview, which set them (keys stay the same objects: CsWinRT
        // keeps one wrapper per ink stroke).
        var begun = starts!;
        var before = Page.CapturePlacement(Strokes, Pictures, s => begun[s]);
        EndPreview();
        foreach (var stroke in Strokes)
        {
            Page.StrokeTransformed(stroke, m); // the ink shows it already
        }
        if (Pictures.Count > 0)
        {
            Page.TransformPictures(Pictures, m); // pictures are redrawn at the new place
        }
        var edit = new TransformEdit(Page, before, Page.CapturePlacement(Strokes, Pictures));
        Area = null;
        Bounds = ContentBounds();
        PlaceFeedback(Bounds);
        return edit;
    }

    /// <summary>Back to where the drag began (the drag was canceled).</summary>
    public void Revert()
    {
        if (!live.IsIdentity)
        {
            Preview(Matrix3x2.Identity);
        }
        EndPreview();
    }

    // Every way out of a drag (commit, nothing moved, revert) ends the page's preview of the strokes.
    private void EndPreview()
    {
        if (starts != null)
        {
            Page.EndStrokePreview(starts.Keys);
            starts = null;
        }
        picturePreview = null;
        slide.X = slide.Y = 0;
    }

    public void Remove()
    {
        Page.Overlay.Children.Remove(frame);
        foreach (var handle in handles)
        {
            Page.Overlay.Children.Remove(handle);
        }
    }

    private Rect ContentBounds()
    {
        var bounds = Rect.Empty;
        foreach (var stroke in Strokes)
        {
            bounds.Union(stroke.BoundingRect);
        }
        foreach (var picture in Pictures)
        {
            bounds.Union(Page.PictureBounds(picture));
        }
        return bounds;
    }

    private void PlaceFeedback(Rect content)
    {
        var r = Inflate(content, Margin);
        Place(frame, r);
        for (int i = 0; i < handles.Length; i++)
        {
            var c = Corner(r, i);
            Place(handles[i], new Rect(c.X - HandleSize / 2, c.Y - HandleSize / 2, HandleSize, HandleSize));
        }
    }

    private static Point Corner(Rect r, int index) => index switch
    {
        0 => new Point(r.Left, r.Top),
        1 => new Point(r.Right, r.Top),
        2 => new Point(r.Right, r.Bottom),
        _ => new Point(r.Left, r.Bottom),
    };

    public static void Place(FrameworkElement element, Rect r)
    {
        XamlCanvas.SetLeft(element, r.X);
        XamlCanvas.SetTop(element, r.Y);
        element.Width = r.Width;
        element.Height = r.Height;
    }

    /// <summary>A rectangle moved/scaled by <paramref name="m"/> (the selection's transforms: no rotation), in double.</summary>
    public static Rect TransformBounds(Rect r, Matrix3x2 m)
    {
        Point Map(double x, double y) => new(x * m.M11 + y * m.M21 + m.M31, x * m.M12 + y * m.M22 + m.M32);
        return new Rect(Map(r.Left, r.Top), Map(r.Right, r.Bottom));
    }

    private static Rect Inflate(Rect r, double by) => new(r.X - by, r.Y - by, r.Width + 2 * by, r.Height + 2 * by);
}
