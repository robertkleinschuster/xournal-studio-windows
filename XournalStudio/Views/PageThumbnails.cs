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
/// (<see cref="PdfExport"/>, this one page, without the background PDF) into a temporary PDF that is rendered (transparent
/// on a PDF page) and deleted at once. Drawn again only for pages that were edited (only pages with a view can be).</item>
/// </list>
/// Pages near the page in view first; only while the pane is shown; a moment after the last change.
/// </summary>
public sealed class PageThumbnails
{
    private static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(500);
    private const uint PixelWidth = (uint)(PagesPane.ThumbWidth * 1.5); // sharp at 150 %
    private const string FilePrefix = "xournal-studio-thumbnails-";

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
        DeleteLeftovers();
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

    /// <summary>These pages were edited: their content layer is drawn again a moment later.</summary>
    public void Edited(IEnumerable<XoppPage> edited)
    {
        var set = edited.ToHashSet();
        foreach (var thumb in pane.Thumbs.Where(t => set.Contains(t.Page)))
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

    /// <summary>The app closes: no refresh any more (its temporary files go as each is rendered).</summary>
    public async Task CloseAsync()
    {
        timer.Stop();
        running?.Cancel();
        await runningTask;
    }

    private async Task RefreshAsync(Task previous)
    {
        running?.Cancel();
        var cancel = new CancellationTokenSource();
        running = cancel;
        await previous; // one refresh at a time (the renderer takes one page at a time anyway)
        int current = pages.Count == 0 ? 0 : pages.CurrentIndex;
        try
        {
            foreach (var thumb in pane.Thumbs.OrderBy(t => Math.Abs(t.Index - current)).ToList())
            {
                cancel.Token.ThrowIfCancellationRequested();
                var page = thumb.Page;
                if (thumb.Background == null && page.IsPdf && session.Pdf is { } pdf)
                {
                    thumb.Background = await pdf.RenderAsync(page.PdfPageIndex, PixelWidth, cancel.Token);
                }
                if (thumb.ContentStale)
                {
                    thumb.ContentStale = false;
                    thumb.Content = await ContentAsync(page, cancel.Token);
                }
            }
        }
        catch (OperationCanceledException)
        {
            // a newer refresh follows (or the pane was closed); stale layers stay marked
        }
        catch (Exception e)
        {
            DebugLog.Write("page thumbnails (the pane shows what it has)", e);
        }
    }

    // What lies on the page, drawn by the PDF export (this page only, without the background PDF) and rendered.
    private static async Task<Microsoft.UI.Xaml.Media.ImageSource?> ContentAsync(XoppPage page, CancellationToken cancel)
    {
        var snapshot = new XDocument(new XElement("xournal", page.Snapshot()));
        string? picture = page.BackgroundImagePath;
        string file = Path.Combine(Path.GetTempPath(), $"{FilePrefix}{Guid.NewGuid():N}.pdf");
        try
        {
            await Task.Run(() => PdfExport.Write(snapshot, null, file, _ => picture), cancel);
            var pdf = await PdfBackground.LoadAsync(file);
            return await pdf.RenderAsync(0, PixelWidth, cancel, transparent: page.IsPdf);
        }
        finally
        {
            Delete(file); // it holds the user's notes: rendered, it goes
        }
    }

    // Thumbnail PDFs a session that ended while rendering (a crash) left in the temp folder.
    private static void DeleteLeftovers()
    {
        try
        {
            foreach (var leftover in Directory.EnumerateFiles(Path.GetTempPath(), FilePrefix + "*.pdf"))
            {
                Delete(leftover);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            DebugLog.Write($"page thumbnails: listing the temp folder: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}");
        }
    }

    private static void Delete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            DebugLog.Write($"page thumbnails: temporary file {path}: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}");
        }
    }
}
