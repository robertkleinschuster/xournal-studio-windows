using System.Xml.Linq;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace XournalStudio.Views;

public enum InkTool
{
    None,
    Lasso,
    Shape,
    PdfArea,
}

/// <summary>
/// The custom tools, after Microsoft's pattern for ink selection ("Pen interactions and Windows Ink"): pen/mouse
/// input on the page, live feedback on the page's overlay layer, and the work done by the ink's own APIs
/// (<c>SelectWithPolyLine</c>, <c>MoveSelected</c>, <c>DeleteSelected</c>, <c>CopySelectedToClipboard</c>).
/// Touch is left to the ScrollView. A selection lives on one page and contains strokes and pictures.
/// </summary>
public sealed class InkTools(UndoStack undo, Func<InkDrawingAttributes> pen)
{
    private const double PdfAreaPixelsPerPoint = 3; // 216 dpi: sharp text when pasted at the original size

    private enum Gesture { None, Shape, Lasso, Move, PdfArea }

    private InkTool tool;
    private Gesture gesture;
    private PageView? gesturePage;
    private Point start, last;
    private Polyline? line;       // shape preview or lasso
    private Rectangle? areaFrame; // PDF area
    private Selection? selection;

    public event EventHandler<(InfoBarSeverity Severity, string Title, string Message)>? Message;

    public InkTool Tool
    {
        get => tool;
        set
        {
            CancelGesture();
            ClearSelection();
            tool = value;
        }
    }

    public ShapeKind Shape { get; set; }

    public bool HasSelection => selection != null;

    /// <summary>Takes pointer input of a page (once per page view).</summary>
    public void Attach(PageView view)
    {
        view.Surface.PointerPressed += (_, e) => OnPressed(view, e);
        view.Surface.PointerMoved += (_, e) => OnMoved(view, e);
        view.Surface.PointerReleased += (_, e) => OnReleased(view, e);
        view.Surface.PointerCaptureLost += (_, _) => Finish(view, last);
        view.Surface.PointerCanceled += (_, _) => CancelGesture();
    }

    // ------------------------------------------------------------------ input

    private void OnPressed(PageView view, PointerRoutedEventArgs e)
    {
        if (tool == InkTool.None || gesture != Gesture.None || e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
        {
            return;
        }
        var point = e.GetCurrentPoint(view.Surface);
        if (!point.Properties.IsLeftButtonPressed || point.Properties.IsEraser)
        {
            return;
        }
        start = last = point.Position;
        switch (tool)
        {
            case InkTool.Shape:
                var attributes = pen();
                line = new Polyline
                {
                    Stroke = new SolidColorBrush(attributes.Color),
                    StrokeThickness = attributes.Size.Width,
                    StrokeLineJoin = PenLineJoin.Round,
                    StrokeStartLineCap = PenLineCap.Round,
                    StrokeEndLineCap = PenLineCap.Round,
                };
                view.Overlay.Children.Add(line);
                gesture = Gesture.Shape;
                break;
            case InkTool.PdfArea:
                if (view.Pdf == null)
                {
                    Message?.Invoke(this, (InfoBarSeverity.Informational, "Keine PDF-Seite",
                        "Bereiche lassen sich aus Seiten mit PDF-Hintergrund kopieren."));
                    return;
                }
                areaFrame = new Rectangle { Stroke = AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
                view.Overlay.Children.Add(areaFrame);
                gesture = Gesture.PdfArea;
                break;
            case InkTool.Lasso:
                if (selection?.View == view && selection.Bounds.Contains(start))
                {
                    gesture = Gesture.Move;
                    break;
                }
                ClearSelection();
                line = new Polyline { Stroke = AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
                line.Points.Add(start);
                view.Overlay.Children.Add(line);
                gesture = Gesture.Lasso;
                break;
        }
        gesturePage = view;
        view.Surface.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void OnMoved(PageView view, PointerRoutedEventArgs e)
    {
        if (gesture == Gesture.None || gesturePage != view)
        {
            return;
        }
        var p = e.GetCurrentPoint(view.Surface).Position;
        switch (gesture)
        {
            case Gesture.Shape:
                line!.Points = ToPointCollection(ShapeGeometry.Outline(Shape, start, p, pen().Size.Width));
                break;
            case Gesture.Lasso:
                line!.Points.Add(p);
                break;
            case Gesture.PdfArea:
                Place(areaFrame!, Normalize(start, p));
                break;
            case Gesture.Move:
                selection!.MoveBy(p.X - last.X, p.Y - last.Y);
                break;
        }
        last = p;
        e.Handled = true;
    }

    private void OnReleased(PageView view, PointerRoutedEventArgs e)
    {
        if (gesture == Gesture.None || gesturePage != view)
        {
            return;
        }
        var p = e.GetCurrentPoint(view.Surface).Position;
        Finish(view, p);
        view.Surface.ReleasePointerCapture(e.Pointer);
        e.Handled = true;
    }

    /// <summary>Completes the gesture at <paramref name="end"/> (released, or the pointer capture was lost).</summary>
    private void Finish(PageView view, Point end)
    {
        if (gesture == Gesture.None || gesturePage != view)
        {
            return;
        }
        var finished = gesture;
        gesture = Gesture.None; // before anything else: releasing the capture raises PointerCaptureLost
        gesturePage = null;
        var lasso = finished == Gesture.Lasso ? line!.Points.ToList() : [];
        RemoveFeedback(view);
        switch (finished)
        {
            case Gesture.Shape:
                AddShape(view.Page, end);
                break;
            case Gesture.Lasso:
                Select(view, lasso);
                break;
            case Gesture.PdfArea:
                CopyPdfArea(view, Normalize(start, end));
                break;
            case Gesture.Move:
                selection?.CompleteMove(undo);
                break;
        }
    }

    private void CancelGesture()
    {
        if (gesturePage != null)
        {
            RemoveFeedback(gesturePage);
        }
        gesture = Gesture.None;
        gesturePage = null;
    }

    private void RemoveFeedback(PageView view)
    {
        if (line != null)
        {
            view.Overlay.Children.Remove(line);
            line = null;
        }
        if (areaFrame != null)
        {
            view.Overlay.Children.Remove(areaFrame);
            areaFrame = null;
        }
    }

    // ------------------------------------------------------------------ tools

    private void AddShape(XoppPage page, Point end)
    {
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

    // Strokes: Windows ink selects those inside the lasso. Pictures: those with all corners inside.
    private void Select(PageView view, List<Point> lasso)
    {
        if (lasso.Count < 3)
        {
            return;
        }
        var page = view.Page;
        var strokeBounds = page.Ink!.SelectWithPolyLine(lasso);
        var strokes = page.Ink.GetStrokes().Where(s => s.Selected).ToList();
        var images = page.Images.Where(i => Corners(XoppImage.GetBounds(i)).All(c => Inside(c, lasso))).ToList();
        if (strokes.Count + images.Count == 0)
        {
            return;
        }
        var bounds = strokes.Count > 0 ? strokeBounds : Rect.Empty;
        foreach (var image in images)
        {
            bounds.Union(XoppImage.GetBounds(image));
        }
        selection = new Selection(view, strokes, images, bounds);
    }

    private async void CopyPdfArea(PageView view, Rect area)
    {
        area.Intersect(new Rect(0, 0, view.Page.Width, view.Page.Height));
        if (area.IsEmpty || area.Width < 4 || area.Height < 4)
        {
            return;
        }
        try
        {
            var (png, width, height) = await view.Pdf!.RenderAreaAsync(view.Page.PdfPageIndex, area, PdfAreaPixelsPerPoint);
            await ClipboardService.CopyImageAsync(png, width, height, new Size(area.Width, area.Height));
            Message?.Invoke(this, (InfoBarSeverity.Success, "Bereich kopiert",
                "Mit Strg+V auf einer Seite einfügen (in Originalgröße)."));
        }
        catch (Exception e)
        {
            DebugLog.Write("copy PDF area", e);
            Message?.Invoke(this, (InfoBarSeverity.Error, "Kopieren fehlgeschlagen", DebugLog.Describe(e)));
        }
    }

    // ------------------------------------------------------------------ selection commands

    public void ClearSelection()
    {
        selection?.Clear();
        selection = null;
    }

    /// <summary>Deletes the selected strokes and pictures (undoable). False: nothing selected.</summary>
    public bool DeleteSelection()
    {
        if (selection == null)
        {
            return false;
        }
        var page = selection.View.Page;
        var edit = new PageEdit(page);
        edit.RemovedStrokes.AddRange(PageEdit.SelectOnly(page, selection.Strokes));
        page.Ink!.DeleteSelected();
        foreach (var image in selection.Images)
        {
            page.RemoveImage(image);
            edit.RemovedImages.Add(image);
        }
        ClearSelection();
        undo.Push(edit);
        return true;
    }

    /// <summary>Copies the selected strokes to the Windows clipboard (ink format, other ink apps paste it too).</summary>
    public bool CopySelection()
    {
        if (selection == null || selection.Strokes.Count == 0)
        {
            return false;
        }
        PageEdit.SelectOnly(selection.View.Page, selection.Strokes);
        selection.View.Page.Ink!.CopySelectedToClipboard();
        return true;
    }

    // ------------------------------------------------------------------ helpers

    private static Brush AccentBrush => (Brush)Application.Current.Resources["AccentFillColorDefaultBrush"];

    private static PointCollection ToPointCollection(IEnumerable<Point> points)
    {
        var collection = new PointCollection();
        foreach (var p in points)
        {
            collection.Add(p);
        }
        return collection;
    }

    private static Rect Normalize(Point a, Point b) => new(a, b);

    private static void Place(FrameworkElement element, Rect r)
    {
        XamlCanvas.SetLeft(element, r.X);
        XamlCanvas.SetTop(element, r.Y);
        element.Width = r.Width;
        element.Height = r.Height;
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

    /// <summary>Selected strokes and pictures of one page with the dashed frame around them.</summary>
    private sealed class Selection
    {
        private readonly Rectangle frame;
        private double movedX, movedY;

        public Selection(PageView view, List<InkStroke> strokes, List<XElement> images, Rect bounds)
        {
            View = view;
            Strokes = strokes;
            Images = images;
            Bounds = Inflate(bounds, 4);
            frame = new Rectangle { Stroke = AccentBrush, StrokeThickness = 1, StrokeDashArray = [4, 2] };
            Place(frame, Bounds);
            view.Overlay.Children.Add(frame);
        }

        public PageView View { get; }

        public List<InkStroke> Strokes { get; }

        public List<XElement> Images { get; }

        public Rect Bounds { get; private set; }

        /// <summary>Moves strokes (Windows ink redraws them), pictures and frame live while dragging.</summary>
        public void MoveBy(double dx, double dy)
        {
            if (Strokes.Count > 0)
            {
                View.Page.Ink!.MoveSelected(new Point(dx, dy)); // the strokes SelectWithPolyLine selected
            }
            movedX += dx;
            movedY += dy;
            foreach (var image in Images)
            {
                if (View.FindPicture(image) is { } picture)
                {
                    picture.RenderTransform = new TranslateTransform { X = movedX, Y = movedY };
                }
            }
            Bounds = new Rect(Bounds.X + dx, Bounds.Y + dy, Bounds.Width, Bounds.Height);
            Place(frame, Bounds);
        }

        /// <summary>Writes the move to the page (file) and records it for undo.</summary>
        public void CompleteMove(UndoStack undo)
        {
            if (movedX == 0 && movedY == 0)
            {
                return;
            }
            View.Page.MoveStrokes(Strokes, movedX, movedY);
            foreach (var image in Images)
            {
                View.Page.MoveImage(image, movedX, movedY); // pictures are redrawn at the new place
            }
            undo.Push(new MoveEdit(View.Page, Strokes.ToList(), Images.ToList(), new Point(movedX, movedY)));
            movedX = movedY = 0;
        }

        public void Clear()
        {
            View.Overlay.Children.Remove(frame);
            if (View.Page.Ink is { } ink)
            {
                foreach (var s in ink.GetStrokes())
                {
                    s.Selected = false;
                }
            }
        }

        private static Rect Inflate(Rect r, double by) => new(r.X - by, r.Y - by, r.Width + 2 * by, r.Height + 2 * by);
    }
}
