using System.Xml.Linq;
using Microsoft.UI.Dispatching;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The pages pane's thumbnails, in two layers per page (<see cref="PageThumb"/>):
/// <list type="bullet">
/// <item>the background PDF's page, rendered by Windows.Data.Pdf from the document's PDF already loaded – once per page;</item>
/// <item>what lies on it – ink, pictures, texts (a page without PDF: its paper too) – drawn as the PDF export draws it
/// (<see cref="PdfExport"/>, this one page, without the background PDF) into memory and rendered (transparent on a PDF
/// page). Drawn again only for pages an edit touched. Nothing of the user's notes is written to a file.</item>
/// </list>
/// Pages near the page in view first; only while the pane is shown; a moment after the last change. A page whose layer
/// fails is logged and left (the others go on); a refresh that is cancelled leaves its page marked to be drawn.
/// </summary>
public sealed class PageThumbnails
{
    private static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(500);
    private const uint PixelWidth = (uint)(PagesPane.ThumbWidth * 1.5); // sharp at 150 %

    private readonly DocumentSession session;
    private readonly PagesPane pane;
    private readonly PagesController pages;
    private readonly DispatcherQueueTimer timer;
    private CancellationTokenSource? running; // the refresh running now (a newer one cancels it)
    private Task runningTask = Task.CompletedTask;
    private bool active;

    public PageThumbnails(DocumentSession session, PagesPane pane, PagesController pages)
    {
        this.session = session;
        this.pane = pane;
        this.pages = pages;
        timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        timer.Interval = Settle;
        timer.IsRepeating = false;
        timer.Tick += (_, _) => runningTask = RefreshAsync(runningTask);
        pane.Shown += (_, _) => Schedule();
    }

    /// <summary>Whether the pane is shown (thumbnails are made only then).</summary>
    public bool Active
    {
        get => active;
        set
        {
            active = value;
            if (active)
            {
                Schedule();
            }
            else
            {
                timer.Stop();
                running?.Cancel();
            }
        }
    }

    /// <summary>An edit was recorded, undone or redone: the pages it touched get their content layer drawn again.</summary>
    public void Edited(InkPages.IPageEdit edit)
    {
        var touched = pages.Views.Where(edit.Touches).Select(v => v.Page).ToHashSet(); // only pages with a view are edited
        foreach (var thumb in pane.Thumbs.Where(t => touched.Contains(t.Page)))
        {
            thumb.ContentStale = true;
        }
        Schedule();
    }

    private void Schedule()
    {
        running?.Cancel();
        if (active)
        {
            timer.Stop();
            timer.Start();
        }
    }

    /// <summary>The app closes: no refresh any more.</summary>
    public async Task CloseAsync()
    {
        timer.Stop();
        running?.Cancel();
        await runningTask;
    }

    private async Task RefreshAsync(Task previous)
    {
        running?.Cancel();
        using var cancel = new CancellationTokenSource();
        running = cancel;
        await previous; // one refresh at a time (the renderer takes one page at a time anyway)
        int current = pages.Count == 0 ? 0 : pages.CurrentIndex;
        var pdf = session.Pdf;
        foreach (var thumb in pane.Thumbs.OrderBy(t => Math.Abs(t.Index - current)).ToList())
        {
            if (cancel.IsCancellationRequested)
            {
                DebugLog.Write("page thumbnails: refresh canceled (a newer one follows, or the pane was closed)");
                break;
            }
            if (thumb.IsGone)
            {
                continue; // the list changed meanwhile (the next refresh draws the new one)
            }
            var page = thumb.Page;
            try
            {
                if (!thumb.BackgroundTried && page.IsPdf && pdf != null)
                {
                    thumb.BackgroundTried = true; // a page the PDF cannot render is not tried again on every change
                    thumb.Background = await pdf.RenderAsync(page.PdfPageIndex, PixelWidth, cancel.Token);
                }
                if (thumb.ContentStale)
                {
                    thumb.Content = await ContentAsync(page, cancel.Token);
                    thumb.ContentStale = false; // only once drawn
                }
            }
            catch (OperationCanceledException)
            {
                DebugLog.Write($"page thumbnails: page {thumb.Index + 1} canceled (drawn by the next refresh)");
                if (page.IsPdf && thumb.Background == null)
                {
                    thumb.BackgroundTried = false;
                }
                break;
            }
            catch (Exception e)
            {
                DebugLog.Write($"page thumbnails: page {thumb.Index + 1} (left as it is)", e);
                thumb.ContentStale = false; // not again until the page is edited
            }
        }
        if (running == cancel)
        {
            running = null;
        }
    }

    // What lies on the page, drawn by the PDF export (this page only, without the background PDF) and rendered.
    private static async Task<Microsoft.UI.Xaml.Media.ImageSource?> ContentAsync(XoppPage page, CancellationToken cancel)
    {
        var snapshot = new XDocument(new XElement("xournal", page.Snapshot()));
        string? picture = page.BackgroundImagePath;
        byte[] data = await Task.Run(() =>
        {
            using var stream = new MemoryStream();
            PdfExport.Write(snapshot, null, stream, _ => picture);
            return stream.ToArray();
        }, cancel);
        using var pdf = await PdfBackground.LoadAsync(data);
        return await pdf.RenderAsync(0, PixelWidth, cancel, transparent: page.IsPdf);
    }
}
