using System.Xml.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace XournalStudio.Views;

/// <summary>
/// The pages in the panel, one child per page in the document's order: the page's <see cref="PageView"/> (InkCanvas,
/// strokes, pictures, texts) only while it is near the viewport or the workspace still needs it
/// (<see cref="InkWorkspace.Holds"/>: its history, the selection, a gesture), otherwise a blank sheet of its size
/// (<see cref="InkPage.CreatePlaceholder"/>) – so opening and memory do not grow with the number of pages, and the layout
/// (scroll extent, page offsets) is the same either way. A page without view is only its XML: its strokes are read
/// (<see cref="XoppPage.ReadStrokes"/>) on the thread pool when it comes near the viewport, its view is created when they
/// are ready, and a released view writes its ink back (<see cref="XoppPage.DetachInk"/>). Views are created anew, never
/// recycled: each InkCanvas belongs to one page.
/// </summary>
/// <param name="strokesRead">A page's strokes were read: its view can be created now (on the UI thread).</param>
public sealed class PageSlots(StackPanel panel, InkWorkspace ink, Action strokesRead)
{
    private readonly List<XoppPage> pages = new(); // in the panel's order
    private readonly Dictionary<XoppPage, PageView> views = new();
    // Strokes being read (or read) for pages without view; a page gets a view only from its own reading (or one made
    // then), so nothing reads a page's element while its view changes it.
    private readonly Dictionary<XoppPage, Task<List<(InkStroke Stroke, XElement Element)>>> reading = new();
    private PdfBackground? pdf;

    public int Count => pages.Count;

    /// <summary>The pages' views that exist now, in page order.</summary>
    public IEnumerable<PageView> Views => pages.Select(p => views.GetValueOrDefault(p)).OfType<PageView>();

    /// <summary>The page's element in the panel (its view or its placeholder).</summary>
    public FrameworkElement ElementAt(int index) => (FrameworkElement)panel.Children[index];

    public int IndexOf(XoppPage page) => pages.IndexOf(page);

    public PageView? ViewOf(XoppPage page) => views.GetValueOrDefault(page);

    /// <summary>Another document's pages: all placeholders (the views come with the next <see cref="Update"/>).</summary>
    public void Show(IReadOnlyList<XoppPage> shown, PdfBackground? background)
    {
        foreach (var view in views.Values)
        {
            view.Close(keepInk: false); // frees its bitmaps and drops its waiting render requests; the document goes
        }
        views.Clear();
        reading.Clear(); // readings still running end unused
        pages.Clear();
        panel.Children.Clear();
        pdf = background;
        foreach (var page in shown)
        {
            pages.Add(page);
            panel.Children.Add(Placeholder(page));
        }
        ink.SetPages([]);
    }

    /// <summary>
    /// The document's PDF got more pages (another PDF's pages appended) or the document has one now: views created from
    /// now on render from <paramref name="background"/> (existing views keep theirs – their pages did not change).
    /// </summary>
    public void UseBackground(PdfBackground background) => pdf = background;

    public void Insert(int index, XoppPage page)
    {
        pages.Insert(index, page);
        panel.Children.Insert(index, Placeholder(page));
    }

    /// <summary>The page left the document: its view (if any) goes with its history.</summary>
    public void Remove(XoppPage page)
    {
        int index = pages.IndexOf(page);
        pages.RemoveAt(index);
        panel.Children.RemoveAt(index);
        reading.Remove(page);
        if (views.Remove(page, out var view))
        {
            view.Close(keepInk: false);
            ink.SetPages(Views);
            ink.Removed(view);
        }
    }

    /// <summary>The page's view, created now if it has none (its strokes read now, or the reading waited for).</summary>
    public PageView Realize(int index)
    {
        if (views.TryGetValue(pages[index], out var existing))
        {
            return existing;
        }
        var page = pages[index];
        // A reading still running is waited for (it is the thread pool's work, no deadlock): the view must not change the
        // element it reads.
        var strokes = reading.Remove(page, out var task) ? task.GetAwaiter().GetResult() : page.ReadStrokes();
        var view = Create(index, strokes);
        ink.SetPages(Views);
        return view;
    }

    /// <summary>
    /// Pages in <paramref name="inView"/> and <paramref name="near"/> get their strokes read; those in view get their
    /// views as soon as their strokes are ready, of those near at most one per update (the nearest: creating a view takes
    /// a while on the UI thread). Views outside <paramref name="keep"/> the workspace does not hold are released.
    /// Rectangles in the panel's coordinates. True: pages near the viewport still wait for their view.
    /// </summary>
    public bool Update(Rect inView, Rect near, Rect keep)
    {
        panel.UpdateLayout(); // offsets of pages just inserted or removed (nothing to do when the layout is current)
        bool changed = false, waiting = false;
        int? nearest = null;
        double nearestDistance = double.MaxValue;
        double middle = inView.Y + inView.Height / 2;
        for (int i = 0; i < pages.Count; i++)
        {
            var page = pages[i];
            var (top, bottom) = Span(i);
            if (views.TryGetValue(page, out var view))
            {
                if (!Overlaps(top, bottom, keep) && !ink.Holds(view))
                {
                    Release(i, view);
                    changed = true;
                }
                continue;
            }
            if (!Overlaps(top, bottom, near))
            {
                if (reading.TryGetValue(page, out var old) && old.IsCompleted)
                {
                    reading.Remove(page); // read for nothing (scrolled past): not kept (one running is waited for)
                }
                continue;
            }
            var task = Read(page);
            if (!task.IsCompleted)
            {
                waiting = true;
            }
            else if (Overlaps(top, bottom, inView))
            {
                CreateRead(i);
                changed = true;
            }
            else
            {
                waiting = true;
                double distance = Math.Min(Math.Abs(top - middle), Math.Abs(bottom - middle));
                if (distance < nearestDistance)
                {
                    nearest = i;
                    nearestDistance = distance;
                }
            }
        }
        if (nearest is { } next)
        {
            CreateRead(next);
            changed = true;
        }
        if (changed)
        {
            ink.SetPages(Views);
        }
        return waiting;
    }

    /// <summary>The page's top and bottom in the panel's coordinates (laid out).</summary>
    public (double Top, double Bottom) Span(int index)
    {
        var element = ElementAt(index);
        double top = element.ActualOffset.Y;
        return (top, top + element.ActualHeight);
    }

    private static bool Overlaps(double top, double bottom, Rect r) => top < r.Bottom && r.Top < bottom && bottom > top;

    // The page's strokes, read on the thread pool (started once); when they are ready an update creates the view.
    private Task<List<(InkStroke Stroke, XElement Element)>> Read(XoppPage page)
    {
        if (!reading.TryGetValue(page, out var task))
        {
            task = Task.Run(page.ReadStrokes);
            reading[page] = task;
            // On the UI thread (its synchronization context); a failed reading surfaces when its view is created.
            task.ContinueWith(_ => strokesRead(), TaskScheduler.FromCurrentSynchronizationContext());
        }
        return task;
    }

    // The view of a page whose reading has completed (its exception, if any, is thrown here – never dropped).
    private void CreateRead(int index)
    {
        var page = pages[index];
        reading.Remove(page, out var task);
        Create(index, task!.GetAwaiter().GetResult());
    }

    private PageView Create(int index, List<(InkStroke Stroke, XElement Element)> strokes)
    {
        var page = pages[index];
        var started = System.Diagnostics.Stopwatch.StartNew();
        var view = new PageView(page, pdf, strokes);
        ink.Attach(view); // hidden at once while a dialog is open (InkWorkspace.InkHidden)
        panel.Children[index] = view; // same size: the layout does not move
        views[page] = view;
        DebugLog.Write($"page {index + 1}: view created ({strokes.Count} strokes, {started.ElapsedMilliseconds} ms)");
        return view;
    }

    private void Release(int index, PageView view)
    {
        var started = System.Diagnostics.Stopwatch.StartNew();
        views.Remove(view.Page);
        panel.Children[index] = Placeholder(view.Page);
        view.Close(keepInk: true);
        DebugLog.Write($"page {index + 1}: view released ({started.ElapsedMilliseconds} ms)");
    }

    private static FrameworkElement Placeholder(XoppPage page) =>
        InkPage.CreatePlaceholder(page.Width, page.Height, new SolidColorBrush(PageView.SheetColor(page)));
}
