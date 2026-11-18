using Microsoft.UI.Dispatching;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The pages pane's thumbnails: the document drawn as the PDF export draws it (<see cref="PdfExport"/>: background, PDF,
/// ink, pictures, texts – also of pages without a view) into a temporary PDF, rendered page by page by Windows.Data.Pdf.
/// Made again a moment after the document changed (one export, not one per stroke), and only while the pane is shown.
/// </summary>
public sealed class PageThumbnails(DocumentSession session, PagesPane pane)
{
    private static readonly TimeSpan Settle = TimeSpan.FromMilliseconds(800);
    private const uint PixelWidth = (uint)(PagesPane.ThumbWidth * 1.5); // sharp at 150 %

    private DispatcherQueueTimer? timer;
    private int generation; // a newer request makes a running one stop
    private string? file;   // the temporary PDF the thumbnails come from (Windows.Data.Pdf may read it while rendering)
    private bool stale = true;

    /// <summary>Whether the pane is shown (thumbnails are made only then).</summary>
    public bool Active
    {
        get => active;
        set
        {
            active = value;
            if (active && stale)
            {
                Invalidate();
            }
        }
    }

    private bool active;

    /// <summary>The document or its pages changed: new thumbnails a moment later.</summary>
    public void Invalidate()
    {
        stale = true;
        generation++;
        if (!active)
        {
            return;
        }
        if (timer == null)
        {
            timer = DispatcherQueue.GetForCurrentThread().CreateTimer();
            timer.Interval = Settle;
            timer.IsRepeating = false;
            timer.Tick += async (_, _) => await RenderAsync();
        }
        timer.Stop();
        timer.Start();
    }

    private async Task RenderAsync()
    {
        int mine = ++generation;
        stale = false;
        var doc = session.Document;
        string target = Path.Combine(Path.GetTempPath(), $"xournal-studio-thumbnails-{Guid.NewGuid():N}.pdf");
        try
        {
            var snapshot = doc.Snapshot(doc.ReferenceBase);
            var pictures = doc.Pages.Select(page => page.BackgroundImagePath).ToList();
            string? background = doc.PdfPath;
            await Task.Run(() => PdfExport.Write(snapshot, background, target, i => i < pictures.Count ? pictures[i] : null));
            if (mine != generation)
            {
                Delete(target);
                return;
            }
            var pdf = await PdfBackground.LoadAsync(target);
            if (file != null)
            {
                Delete(file);
            }
            file = target;
            for (int i = 0; i < pdf.PageCount && i < pane.Count; i++)
            {
                if (mine != generation || !active)
                {
                    stale = true;
                    return;
                }
                pane.SetThumbnail(i, await pdf.RenderAsync(i, PixelWidth));
            }
        }
        catch (Exception e)
        {
            DebugLog.Write("page thumbnails (the pane shows the page sizes only)", e);
            Delete(target);
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
