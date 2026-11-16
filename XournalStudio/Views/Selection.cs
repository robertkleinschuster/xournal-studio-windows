using System.Numerics;
using System.Xml.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace XournalStudio.Views;

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
    // (XoppPage.BeginPreview).
    private Dictionary<InkStroke, Matrix3x2>? starts;
    private Matrix3x2 live = Matrix3x2.Identity;

    public Selection(PageView view, List<InkStroke> strokes, List<XElement> images, Rect? area)
    {
        View = view;
        Strokes = strokes.DistinctBy(s => s.Id).ToList(); // each stroke and picture once: its element is moved once
        Images = images.Distinct().ToList();
        Area = area;
        Bounds = area ?? ContentBounds();
        frame = new Rectangle { Stroke = AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
        view.Overlay.Children.Add(frame);
        handles = HasContent
            ? Enumerable.Range(0, 4).Select(_ => new Rectangle { Width = HandleSize, Height = HandleSize, Fill = AccentBrush }).ToArray()
            : [];
        foreach (var handle in handles)
        {
            view.Overlay.Children.Add(handle);
        }
        PlaceFeedback(Bounds);
    }

    public PageView View { get; }

    public XoppPage Page => View.Page;

    public List<InkStroke> Strokes { get; }

    public List<XElement> Images { get; }

    /// <summary>The rectangle drawn on a PDF page (copying renders it); null once the content was moved.</summary>
    public Rect? Area { get; private set; }

    /// <summary>The area, otherwise what the strokes and pictures cover (page points).</summary>
    public Rect Bounds { get; private set; }

    public bool HasContent => Strokes.Count + Images.Count > 0;

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

    /// <summary>Shows the content transformed by <paramref name="m"/> (relative to where it was when the drag began).</summary>
    public void Preview(Matrix3x2 m)
    {
        if (starts == null)
        {
            starts = new Dictionary<InkStroke, Matrix3x2>();
            foreach (var stroke in Strokes)
            {
                starts[stroke] = stroke.PointTransform; // (a stroke listed twice is taken once)
                Page.BeginPreview(stroke, stroke.PointTransform);
            }
        }
        foreach (var (stroke, start) in starts)
        {
            stroke.PointTransform = start * m;
        }
        foreach (var image in Images)
        {
            if (View.FindPicture(image) is { } picture)
            {
                PageView.Place(picture, image, m);
            }
        }
        live = m;
        PlaceFeedback(XoppImage.Transform(Bounds, m));
    }

    /// <summary>Writes the previewed transform to the page and records it for undo. False: nothing changed.</summary>
    public bool Commit(UndoStack undo)
    {
        var m = live;
        live = Matrix3x2.Identity;
        if (m.IsIdentity)
        {
            EndPreview();
            return false;
        }
        // A transform other than identity comes only from Preview, which set them (keys stay the same objects: CsWinRT
        // keeps one wrapper per ink stroke).
        var begun = starts!;
        var before = Page.CapturePlacement(Strokes, Images, s => begun[s]);
        EndPreview();
        foreach (var stroke in Strokes)
        {
            Page.RecordTransform(stroke, m); // the ink shows it already
        }
        Page.TransformImages(Images, m); // pictures are redrawn at the new place
        undo.Push(new TransformEdit(Page, before, Page.CapturePlacement(Strokes, Images)));
        Area = null;
        Bounds = ContentBounds();
        PlaceFeedback(Bounds);
        return true;
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
            Page.EndPreview(starts.Keys);
            starts = null;
        }
    }

    public void Remove()
    {
        View.Overlay.Children.Remove(frame);
        foreach (var handle in handles)
        {
            View.Overlay.Children.Remove(handle);
        }
    }

    private Rect ContentBounds()
    {
        var bounds = Rect.Empty;
        foreach (var stroke in Strokes)
        {
            bounds.Union(stroke.BoundingRect);
        }
        foreach (var image in Images)
        {
            bounds.Union(XoppImage.GetBounds(image));
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

    private static Rect Inflate(Rect r, double by) => new(r.X - by, r.Y - by, r.Width + 2 * by, r.Height + 2 * by);
}
