using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace XournalStudio.Views;

/// <summary>
/// The document's pages in the ScrollView: one <see cref="PageView"/> per page wired to the tools and undo, zoom,
/// the page in view, PDF rendering near the viewport, inserting and removing pages.
/// </summary>
public sealed class PagesController
{
    private readonly ScrollView scroller;
    private readonly StackPanel panel;
    private readonly ToolController tools;
    private readonly InkTools inkTools;
    private readonly UndoStack undo;
    private readonly Func<bool> recognizeShapes;
    private readonly DispatcherQueueTimer viewTimer;
    private XoppDocument? document;
    private PdfBackground? pdf;
    private bool fitWidthPending = true;
    private float? requestedZoom; // asked for with ZoomTo, until the ScrollView shows it

    public PagesController(ScrollView scroller, StackPanel panel, ToolController tools, InkTools inkTools,
        UndoStack undo, Func<bool> recognizeShapes)
    {
        this.scroller = scroller;
        this.panel = panel;
        this.tools = tools;
        this.inkTools = inkTools;
        this.undo = undo;
        this.recognizeShapes = recognizeShapes;

        viewTimer = scroller.DispatcherQueue.CreateTimer();
        viewTimer.Interval = TimeSpan.FromMilliseconds(120);
        viewTimer.IsRepeating = false;
        viewTimer.Tick += (_, _) => UpdateVisiblePages();
        scroller.ViewChanged += (_, _) =>
        {
            if (requestedZoom is { } z && Math.Abs(scroller.ZoomFactor - z) < 1e-4)
            {
                requestedZoom = null;
            }
            viewTimer.Start();
        };
        scroller.SizeChanged += (_, _) =>
        {
            if (fitWidthPending)
            {
                FitWidth();
            }
            viewTimer.Start();
        };
    }

    /// <summary>Pages were inserted or removed (the document changed).</summary>
    public event EventHandler? PagesChanged;

    public IEnumerable<PageView> Views => panel.Children.OfType<PageView>();

    /// <summary>The page in the middle of the viewport (or the last one above it).</summary>
    public int CurrentIndex
    {
        get
        {
            double middle = scroller.ViewportHeight / 2;
            int index = 0, i = 0;
            foreach (var view in Views)
            {
                if (BoundsInViewport(view).Top <= middle)
                {
                    index = i;
                }
                i++;
            }
            return index;
        }
    }

    public PageView? CurrentView => Views.ElementAtOrDefault(CurrentIndex);

    public double VerticalOffset => scroller.VerticalOffset;
    public float ZoomFactor => requestedZoom ?? scroller.ZoomFactor;

    public void RestorePosition(DocumentPosition? position)
    {
        if (position == null || position.Zoom < scroller.MinZoomFactor || position.Zoom > scroller.MaxZoomFactor)
        {
            return;
        }
        fitWidthPending = false;
        requestedZoom = position.Zoom;
        scroller.ZoomTo(position.Zoom, System.Numerics.Vector2.Zero,
            new ScrollingZoomOptions(ScrollingAnimationMode.Disabled));
        // ScrollView lays out the zoomed content asynchronously; restore its offset after that layout.
        scroller.DispatcherQueue.TryEnqueue(() =>
        {
            panel.UpdateLayout();
            scroller.ScrollTo(0, position.VerticalOffset,
                new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        });
    }

    public void Show(XoppDocument doc, PdfBackground? background)
    {
        document = doc;
        pdf = background;
        inkTools.CancelGestureOn(_ => true); // a drag on the old pages ends with them
        // A new document starts with the pen as ink (otherwise its first tip stroke could stay without ink until a hover
        // reaches a page); an eraser end still touching while a document is replaced is the rare case.
        inkTools.ReleaseEraserEnd();
        inkTools.Selection.Clear();
        undo.Clear();
        panel.Children.Clear();
        foreach (var page in doc.Pages)
        {
            panel.Children.Add(CreateView(page));
        }
        OnViewsChanged();
        fitWidthPending = true;
        scroller.ScrollTo(0, 0, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        FitWidth();
    }

    /// <summary>
    /// The page under a point given in <paramref name="from"/>'s coordinates (page points), if it is another page, with
    /// the offset from <paramref name="from"/>'s coordinates to its own (pages differ by a translation: same zoom).
    /// </summary>
    public (PageView Page, System.Numerics.Vector2 Offset)? OtherPageAt(PageView from, Point p)
    {
        foreach (var view in Views)
        {
            if (view == from)
            {
                continue;
            }
            var q = from.InkCanvas.TransformToVisual(view.InkCanvas).TransformPoint(p);
            if (q.X >= 0 && q.Y >= 0 && q.X <= view.Page.Width && q.Y <= view.Page.Height)
            {
                return (view, new System.Numerics.Vector2((float)(q.X - p.X), (float)(q.Y - p.Y)));
            }
        }
        return null;
    }

    /// <summary>A graph page (size of the current one) after the page in view, scrolled into view.</summary>
    public void InsertAfterCurrent()
    {
        var doc = document!;
        int index = doc.Pages.Count == 0 ? -1 : CurrentIndex;
        var current = index >= 0 ? doc.Pages[index] : null;
        var page = XoppPage.CreateGraph(current?.Width ?? XoppDocument.A4Width, current?.Height ?? XoppDocument.A4Height);
        doc.Pages.Insert(index + 1, page);
        var view = CreateView(page);
        panel.Children.Insert(index + 1, view);
        OnViewsChanged();
        PagesChanged?.Invoke(this, EventArgs.Empty);
        ScrollIntoView(view);
    }

    /// <summary>Removes this page (taken by the page, not an index: the document may have changed meanwhile).</summary>
    public void Remove(XoppPage page)
    {
        var doc = document!;
        int index = doc.Pages.IndexOf(page);
        if (index < 0)
        {
            return; // not in the shown document any more
        }
        var view = panel.Children[index] as PageView;
        // A drag on this page or below ends: those pages move up under the pen. One above goes on.
        inkTools.CancelGestureOn(gestureView => panel.Children.IndexOf(gestureView) >= index);
        if (inkTools.Selection.Current?.View == view)
        {
            inkTools.Selection.Clear(); // a selection on another page stays
        }
        doc.Pages.RemoveAt(index);
        panel.Children.RemoveAt(index);
        undo.Forget(page);
        OnViewsChanged();
        PagesChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Scrolls to the page with this index (if the document has it).</summary>
    public void ScrollToPage(int index)
    {
        if (Views.ElementAtOrDefault(index) is { } view)
        {
            ScrollIntoView(view);
        }
    }

    /// <summary>Scrolls so the page's top is at the top of the viewport.</summary>
    public void ScrollIntoView(PageView view)
    {
        panel.UpdateLayout();
        double y = view.ActualOffset.Y * (requestedZoom ?? scroller.ZoomFactor); // ZoomTo applies later
        scroller.ScrollTo(0, y, new ScrollingScrollOptions(ScrollingAnimationMode.Auto));
    }

    public void FitWidth()
    {
        if (scroller.ViewportWidth <= 0 || document == null || document.Pages.Count == 0)
        {
            return;
        }
        fitWidthPending = false;
        double pageWidth = document.Pages.Max(p => p.Width) + 32;
        float zoom = (float)Math.Clamp(scroller.ViewportWidth / pageWidth, scroller.MinZoomFactor, scroller.MaxZoomFactor);
        requestedZoom = zoom;
        scroller.ZoomTo(zoom, System.Numerics.Vector2.Zero, new ScrollingZoomOptions(ScrollingAnimationMode.Disabled));

        // Test hook: start at page XOURNAL_STUDIO_TEST_PAGE (1-based).
        if (int.TryParse(Environment.GetEnvironmentVariable("XOURNAL_STUDIO_TEST_PAGE"), out int testPage) &&
            testPage >= 1 && testPage <= panel.Children.Count)
        {
            panel.UpdateLayout();
            double y = panel.Children[testPage - 1].ActualOffset.Y * zoom;
            scroller.ScrollTo(0, y, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        }
    }

    public void Zoom(float factor)
    {
        var center = new System.Numerics.Vector2((float)scroller.ViewportWidth / 2, (float)scroller.ViewportHeight / 2);
        float zoom = (float)Math.Clamp((requestedZoom ?? scroller.ZoomFactor) * factor, scroller.MinZoomFactor, scroller.MaxZoomFactor);
        requestedZoom = zoom;
        scroller.ZoomTo(zoom, center);
    }

    private PageView CreateView(XoppPage page)
    {
        var view = new PageView(page, pdf);
        inkTools.Attach(view);
        ToolController.Attach(view);
        view.Ink.StrokesCollected += (_, e) => OnStrokesCollected(view, e.Strokes);
        view.Ink.StrokesErased += (_, e) =>
        {
            // A selection must not keep strokes that are gone (it would copy, move or bring them back) – also when they
            // are put back below: as copies, not the selected strokes; the selection ends.
            var erased = e.Strokes.Select(s => s.Id).ToHashSet();
            if (inkTools.Selection.Current?.Strokes.Any(s => erased.Contains(s.Id)) == true)
            {
                inkTools.Selection.Clear();
            }
            if (tools.PartialErasing && !tools.EraserActive)
            {
                // Fallback: the eraser end touched with partial erasing on before its hover handed it to the app
                // (InkTools.TakeEraserEnd), and Windows ink erased whole strokes. They are put back, and from its next
                // press on it goes to the partial eraser until the tip hovers again.
                // (The copies are added on top: InkStrokeContainer has no insert at a position.)
                PageEdit.RestoreStrokes(page, e.Strokes.ToList());
                tools.EraserEndTakenOver = true; // the next eraser-end touch is a press the partial eraser gets (InkTools)
                return;
            }
            var edit = new PageEdit(page);
            edit.RemovedStrokes.AddRange(e.Strokes);
            undo.Push(edit);
        };
        return view;
    }

    /// <summary>Records new ink for undo; with shape recognition on, hand-drawn shapes become clean ones.</summary>
    private async void OnStrokesCollected(PageView view, IReadOnlyList<InkStroke> strokes)
    {
        var shown = document;
        var page = view.Page;
        var edit = new PageEdit(page);
        foreach (var stroke in strokes.ToList())
        {
            InkStroke? shape = null;
            if (recognizeShapes() && !stroke.DrawingAttributes.DrawAsHighlighter)
            {
                try
                {
                    shape = await ShapeRecognition.RecognizeAsync(stroke);
                }
                catch (Exception e)
                {
                    DebugLog.Write("shape recognition (stroke kept as drawn)", e);
                }
                if (document != shown)
                {
                    return; // another document was opened meanwhile: this one's history is gone
                }
            }
            if (shape != null && page.Ink!.GetStrokes().Contains(stroke))
            {
                PageEdit.RemoveStrokes(page, [stroke]);
                page.Ink!.AddStroke(shape);
                edit.AddedStrokes.Add(shape);
            }
            else
            {
                edit.AddedStrokes.Add(stroke);
            }
        }
        undo.Push(edit);
    }

    private void OnViewsChanged()
    {
        tools.SetPages(Views);
        viewTimer.Start();
    }

    /// <summary>Renders PDF backgrounds of pages near the viewport at the current zoom; frees the others.</summary>
    private void UpdateVisiblePages()
    {
        var viewport = new Rect(0, 0, scroller.ViewportWidth, scroller.ViewportHeight);
        var near = new Rect(0, -viewport.Height, viewport.Width, viewport.Height * 3);
        double scale = scroller.ZoomFactor * (scroller.XamlRoot?.RasterizationScale ?? 1.0);
        foreach (var view in Views)
        {
            view.UpdateBackground(Intersects(BoundsInViewport(view), near), scale);
        }
        tools.SetCurrentPage(CurrentView);
    }

    private Rect BoundsInViewport(PageView view) =>
        view.TransformToVisual(scroller).TransformBounds(new Rect(0, 0, view.Width, view.Height));

    private static bool Intersects(Rect a, Rect b) =>
        a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;
}
