using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The document's pages in the ScrollView: one <see cref="PageView"/> per page wired to the tools and undo, zoom,
/// the page in view, PDF rendering near the viewport, inserting and removing pages.
/// </summary>
public sealed class PagesController
{
    private readonly ScrollView scroller;
    private readonly StackPanel panel;
    private readonly InkWorkspace ink;
    private readonly DispatcherQueueTimer viewTimer;
    private XoppDocument? document;
    private PdfBackground? pdf;
    private bool fitWidthPending = true;
    private float? requestedZoom; // asked for with ZoomTo, until the ScrollView shows it

    public PagesController(ScrollView scroller, StackPanel panel, InkWorkspace ink)
    {
        this.scroller = scroller;
        this.panel = panel;
        this.ink = ink;

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
        ink.Reset();
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
        var view = (PageView)panel.Children[index];
        // A drag on this page or below ends: those pages move up under the pen. One above goes on.
        ink.Removing(view, gestureView => panel.Children.IndexOf(gestureView) >= index);
        doc.Pages.RemoveAt(index);
        panel.Children.RemoveAt(index);
        ink.Removed(view);
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
        ink.Attach(view);
        return view;
    }

    private void OnViewsChanged()
    {
        ink.SetPages(Views);
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
            // The page of a dragged selection stays shown: its pictures are being moved (and may be far off by now).
            bool dragged = ink.Selection.IsDragging && ink.Selection.Current?.Page == view;
            view.UpdateBackground(dragged || Intersects(BoundsInViewport(view), near), scale);
        }
        ink.SetCurrentPage(CurrentView);
    }

    private Rect BoundsInViewport(PageView view) =>
        view.TransformToVisual(scroller).TransformBounds(new Rect(0, 0, view.Width, view.Height));

    private static bool Intersects(Rect a, Rect b) =>
        a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;
}
