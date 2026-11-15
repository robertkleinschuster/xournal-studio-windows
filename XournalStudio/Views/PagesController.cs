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
        scroller.ViewChanged += (_, _) => viewTimer.Start();
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

    public void Show(XoppDocument doc, PdfBackground? background)
    {
        document = doc;
        pdf = background;
        inkTools.ClearSelection();
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

    public void Remove(int index)
    {
        var doc = document!;
        var page = doc.Pages[index];
        doc.Pages.RemoveAt(index);
        panel.Children.RemoveAt(index);
        inkTools.ClearSelection();
        undo.Forget(page);
        OnViewsChanged();
        PagesChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Scrolls so the page's top is at the top of the viewport.</summary>
    public void ScrollIntoView(PageView view)
    {
        panel.UpdateLayout();
        double y = view.ActualOffset.Y * scroller.ZoomFactor;
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
        scroller.ZoomTo((float)Math.Clamp(scroller.ZoomFactor * factor, scroller.MinZoomFactor, scroller.MaxZoomFactor), center);
    }

    private PageView CreateView(XoppPage page)
    {
        var view = new PageView(page, pdf);
        inkTools.Attach(view);
        ToolController.Attach(view);
        view.Ink.StrokesCollected += (_, e) => OnStrokesCollected(view, e.Strokes);
        view.Ink.StrokesErased += (_, e) =>
        {
            var edit = new PageEdit(page);
            edit.RemovedStrokes.AddRange(e.Strokes);
            undo.Push(edit);
        };
        return view;
    }

    /// <summary>Records new ink for undo; with shape recognition on, hand-drawn shapes become clean ones.</summary>
    private async void OnStrokesCollected(PageView view, IReadOnlyList<InkStroke> strokes)
    {
        var page = view.Page;
        var edit = new PageEdit(page);
        foreach (var stroke in strokes.ToList())
        {
            var shape = recognizeShapes() && !stroke.DrawingAttributes.DrawAsHighlighter
                ? await ShapeRecognition.RecognizeAsync(stroke) : null;
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
