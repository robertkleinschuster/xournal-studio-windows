using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The document's pages in the ScrollView: zoom, the page in view, which pages have a view (<see cref="PageSlots"/>:
/// only those near the viewport), PDF rendering near the viewport, inserting and removing pages.
/// </summary>
public sealed class PagesController
{
    private readonly ScrollView scroller;
    private readonly StackPanel panel;
    private readonly InkWorkspace ink;
    private readonly PageSlots slots;
    private readonly DispatcherQueueTimer viewTimer;
    private XoppDocument? document;
    private bool fitWidthPending = true;
    private float? requestedZoom; // asked for with ZoomTo, until the ScrollView shows it

    public PagesController(ScrollView scroller, StackPanel panel, InkWorkspace ink)
    {
        this.scroller = scroller;
        this.panel = panel;
        this.ink = ink;
        slots = new PageSlots(panel, ink, UpdateVisiblePages); // a page's strokes are read: its view at once

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
            // Throttled, not restarted: during a long scroll (auto-scroll, fling) the pages coming into view are drawn
            // every 120 ms instead of only when the view rests.
            if (!viewTimer.IsRunning)
            {
                viewTimer.Start();
            }
        };
        scroller.ZoomCompleted += OnZoomCompleted;
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

    /// <summary>The pages' views that exist now (near the viewport, or held by the ink workspace).</summary>
    public IEnumerable<PageView> Views => slots.Views;

    /// <summary>A document with pages is shown.</summary>
    public bool HasPages => slots.Count > 0;

    /// <summary>The pages' ink is hidden while a dialog is open (see <see cref="Dialogs"/>).</summary>
    public bool InkHidden
    {
        get => ink.InkHidden;
        set => ink.InkHidden = value;
    }

    /// <summary>The page in the middle of the viewport (or the last one above it).</summary>
    public int CurrentIndex
    {
        get
        {
            panel.UpdateLayout(); // pages just inserted or removed are placed (nothing to do when the layout is current)
            var viewport = ViewportInPanel();
            double middle = viewport.Y + viewport.Height / 2;
            int index = 0;
            for (int i = 0; i < slots.Count && slots.Span(i).Top <= middle; i++)
            {
                index = i;
            }
            return index;
        }
    }

    /// <summary>The view of the page in view, created now if it has none yet (null: no pages).</summary>
    public PageView? CurrentPageView() => slots.Count == 0 ? null : slots.Realize(CurrentIndex);

    /// <summary>The page's view, created now if it has none (null: the page is no longer in the document).</summary>
    public PageView? ViewOf(XoppPage page) => slots.IndexOf(page) is >= 0 and var index ? slots.Realize(index) : null;

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
        // The ScrollView zooms asynchronously; the offset (in zoomed units) is restored when this zoom has completed
        // (ZoomCompleted, by the request's correlation id) – not at a guessed moment: a ScrollTo before the zoom applied
        // was lost once opening became fast (pages without views).
        int zoom = scroller.ZoomTo(position.Zoom, System.Numerics.Vector2.Zero,
            new ScrollingZoomOptions(ScrollingAnimationMode.Disabled));
        restoreOffset = (zoom, position.VerticalOffset);
    }

    // The offset to restore once the zoom request with this correlation id has completed.
    private (int Zoom, double Offset)? restoreOffset;

    private void OnZoomCompleted(ScrollView sender, ScrollingZoomCompletedEventArgs e)
    {
        if (restoreOffset is { } r && r.Zoom == e.CorrelationId)
        {
            restoreOffset = null;
            DebugLog.Write($"position: zoom {sender.ZoomFactor:0.###} applied, offset {r.Offset:0} restored");
            panel.UpdateLayout();
            scroller.ScrollTo(0, r.Offset, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        }
    }

    public void Show(XoppDocument doc, PdfBackground? background)
    {
        document = doc;
        restoreOffset = null; // the previous document's
        ink.Reset();
        slots.Show(doc.Pages, background); // placeholders: the views near the viewport come with the next update
        MarkCurrentPage();
        fitWidthPending = true;
        scroller.ScrollTo(0, 0, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        FitWidth();
        viewTimer.Start();
    }

    /// <summary>A graph page (size of the current one) after the page in view, scrolled into view.</summary>
    public void InsertAfterCurrent()
    {
        var doc = document!;
        int index = doc.Pages.Count == 0 ? -1 : CurrentIndex;
        var current = index >= 0 ? doc.Pages[index] : null;
        var page = XoppPage.CreateGraph(current?.Width ?? XoppDocument.A4Width, current?.Height ?? XoppDocument.A4Height);
        doc.Pages.Insert(index + 1, page);
        slots.Insert(index + 1, page);
        slots.Realize(index + 1); // it comes into view now
        viewTimer.Start();
        MarkCurrentPage();
        PagesChanged?.Invoke(this, EventArgs.Empty);
        ScrollIntoView(index + 1);
    }

    /// <summary>
    /// Pages with the document's PDF pages <paramref name="pdfPages"/> (0-based) as background after the page in view,
    /// the first scrolled into view; <paramref name="background"/> is the document's PDF with them (loaded again after
    /// pages were appended to it).
    /// </summary>
    public void InsertPdfPagesAfterCurrent(IReadOnlyList<int> pdfPages, PdfBackground background)
    {
        var doc = document!;
        slots.UseBackground(background);
        var sizes = background.GetPageSizes();
        int index = doc.Pages.Count == 0 ? -1 : CurrentIndex;
        foreach (int pdfPage in pdfPages)
        {
            var page = XoppPage.CreatePdf(sizes[pdfPage].Width, sizes[pdfPage].Height, pdfPage);
            index++;
            doc.Pages.Insert(index, page);
            slots.Insert(index, page);
        }
        int first = index - pdfPages.Count + 1;
        slots.Realize(first);
        viewTimer.Start();
        MarkCurrentPage();
        PagesChanged?.Invoke(this, EventArgs.Empty);
        ScrollIntoView(first);
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
        if (slots.ViewOf(page) is { } view)
        {
            // A drag on this page or below ends: those pages move up under the pen. One above goes on.
            ink.Removing(view, gestureView => doc.Pages.IndexOf(((PageView)gestureView).Page) >= index);
        }
        doc.Pages.RemoveAt(index);
        slots.Remove(page); // its view (if any) and its history go
        viewTimer.Start();
        MarkCurrentPage();
        PagesChanged?.Invoke(this, EventArgs.Empty);
    }

    // The page in view (the toolbar's, the ruler's) is outlined in a document with more than one page – decided by the
    // document, as only the pages near the viewport have views.
    private void MarkCurrentPage() => ink.MarkCurrentPage = document?.Pages.Count > 1;

    /// <summary>Scrolls to the page with this index (if the document has it).</summary>
    public void ScrollToPage(int index)
    {
        if (index >= 0 && index < slots.Count)
        {
            ScrollIntoView(index);
        }
    }

    // Scrolls so the page's top is at the top of the viewport.
    private void ScrollIntoView(int index)
    {
        panel.UpdateLayout();
        double y = slots.ElementAt(index).ActualOffset.Y * (requestedZoom ?? scroller.ZoomFactor); // ZoomTo applies later
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
            testPage >= 1 && testPage <= slots.Count)
        {
            panel.UpdateLayout();
            double y = slots.ElementAt(testPage - 1).ActualOffset.Y * zoom;
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

    /// <summary>
    /// Pages near the viewport (one viewport height above and below) get their views, those beyond two heights lose
    /// theirs; renders the PDF backgrounds of the views near the viewport at the current zoom and frees the others.
    /// </summary>
    private void UpdateVisiblePages()
    {
        if (slots.Count == 0 || scroller.ViewportHeight <= 0)
        {
            return;
        }
        var viewport = ViewportInPanel();
        var near = Grow(viewport, 1);
        if (slots.Update(viewport, near, Grow(viewport, 2)))
        {
            viewTimer.Start(); // the next page near the viewport in the next round
        }
        double scale = scroller.ZoomFactor * (scroller.XamlRoot?.RasterizationScale ?? 1.0);
        var shown = new List<(PageView View, bool InView)>();
        foreach (var view in slots.Views)
        {
            var (top, bottom) = slots.Span(slots.IndexOf(view.Page));
            // The page of a dragged selection stays shown: its pictures are being moved (and may be far off by now).
            bool dragged = ink.Selection.IsDragging && ink.Selection.Current?.Page == view;
            if (dragged || (top < near.Bottom && near.Top < bottom))
            {
                shown.Add((view, top < viewport.Bottom && viewport.Top < bottom));
            }
            else
            {
                view.UpdateBackground(false, scale); // first: its waiting render request is dropped
            }
        }
        // The pages in view first: the renderer takes the requests one at a time in order.
        foreach (var (view, _) in shown.OrderByDescending(s => s.InView))
        {
            view.UpdateBackground(true, scale);
        }
        ink.SetCurrentPage(CurrentPageView());
    }

    // The viewport in the panel's coordinates (page units: the zoom is taken out).
    private Rect ViewportInPanel() =>
        scroller.TransformToVisual(panel).TransformBounds(new Rect(0, 0, scroller.ViewportWidth, scroller.ViewportHeight));

    private static Rect Grow(Rect r, double heights) =>
        new(r.X, r.Y - r.Height * heights, r.Width, r.Height * (1 + 2 * heights));
}
