using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// A page view's background bitmap: its PDF page (Windows.Data.Pdf) or its picture background, rendered sharp enough for
/// the zoom while the page is near the viewport and freed otherwise (<see cref="Release"/>). A bitmap that is replaced is
/// freed (<see cref="PictureRenderer.Release"/>).
/// </summary>
public sealed class PageBackground(XoppPage page, PdfBackground? pdf, Image image)
{
    /// <summary>The widest bitmap rendered for a page or picture (pixels).</summary>
    public const uint MaxPixelWidth = 3200;

    private uint renderedPixelWidth;
    private uint requestedPixelWidth;

    /// <summary>Renders the PDF page or decodes the picture background for <paramref name="pixelsPerPoint"/>.</summary>
    public void Update(double pixelsPerPoint)
    {
        if (page.IsImageBackground)
        {
            ShowImageBackground(pixelsPerPoint);
        }
        RenderPdf(pixelsPerPoint);
    }

    /// <summary>The page left the viewport: the bitmap is freed, a waiting render dropped, a running load not shown.</summary>
    public void Release()
    {
        Show(null);
        CancelPendingRender();
        renderedPixelWidth = requestedPixelWidth = 0;
        imageFailed = false; // looked for again when the page comes back (a sync may have brought it)
        imageRequested = imageShown = 0;
        releases++;
    }

    // A picture background, stretched over the page as upstream draws it; decoded at about the shown size, again when
    // zoomed in a lot. The file is read into memory off the UI thread (not held open: a sync or rename is not blocked) and
    // decoded like the pictures (PictureRenderer); a decoded bitmap is shown when it is sharper than the one shown (the
    // previous one stays meanwhile). A picture that cannot be read or decoded is logged and not tried again (the shown
    // one, if any, stays) until the page leaves the viewport and comes back.
    private bool imageFailed;
    private uint imageRequested, imageShown; // pixel widths
    private int releases; // loads started before a release are not shown

    private async void ShowImageBackground(double pixelsPerPoint)
    {
        string? path = page.BackgroundImagePath;
        uint wanted = (uint)Math.Min(MaxPixelWidth, Math.Ceiling(page.Width * pixelsPerPoint));
        if (path == null || imageFailed || imageRequested * 1.6 >= wanted)
        {
            return;
        }
        imageRequested = wanted; // not started again on every viewport update
        int release = releases;
        try
        {
            byte[] data = await Task.Run(() => File.ReadAllBytes(path));
            var bitmap = await PictureRenderer.LoadAsync(data, wanted, $"picture background {path}");
            if (release == releases && wanted > imageShown)
            {
                Show(bitmap);
                imageShown = wanted;
                imageRequested = Math.Max(imageRequested, wanted); // requested ≥ shown (a failure lowers it)
            }
            else
            {
                PictureRenderer.Release(bitmap);
            }
        }
        catch (Exception e) // any failure of reading or decoding (async void: nothing may escape); logged
        {
            DebugLog.Write($"picture background {path}", e);
            if (release == releases && wanted == imageRequested) // not a load a newer one replaced
            {
                imageFailed = true; // a sharper decode failed: the shown one (if any) stays, not retried
                imageRequested = imageShown;
            }
        }
    }

    // The request of the PDF page's rendering that has not been shown yet; cancelled when the page leaves the viewport or
    // another width is wanted, so a request that waits for the renderer is dropped instead of rendered for nothing.
    private CancellationTokenSource? pendingRender;

    private void CancelPendingRender()
    {
        pendingRender?.Cancel();
        pendingRender = null;
    }

    private async void RenderPdf(double pixelsPerPoint)
    {
        if (pdf == null)
        {
            return;
        }
        uint wanted = (uint)Math.Min(MaxPixelWidth, Math.Ceiling(page.Width * pixelsPerPoint));
        bool sharpEnough = renderedPixelWidth >= wanted * 0.9 && renderedPixelWidth <= wanted * 1.6;
        if (sharpEnough)
        {
            CancelPendingRender(); // e.g. zoomed back before a request for another width ran
            requestedPixelWidth = renderedPixelWidth;
            return;
        }
        if (requestedPixelWidth == wanted)
        {
            return;
        }

        CancelPendingRender();
        var request = pendingRender = new CancellationTokenSource();
        requestedPixelWidth = wanted;
        try
        {
            var bitmap = await pdf.RenderAsync(page.PdfPageIndex, wanted, request.Token);
            if (requestedPixelWidth == wanted && bitmap != null)
            {
                Show(bitmap);
                renderedPixelWidth = wanted;
            }
            else
            {
                PictureRenderer.Release(bitmap);
                DebugLog.Write($"pdf page {page.PdfPageIndex} {wanted}px: not shown (no longer wanted)");
            }
        }
        catch (OperationCanceledException) when (request.IsCancellationRequested) // dropped by CancelPendingRender
        {
            DebugLog.Write($"pdf page {page.PdfPageIndex} {wanted}px: dropped before rendering");
        }
        catch (Exception e) // async void: nothing may escape; logged, the next viewport update tries again
        {
            DebugLog.Write($"render pdf page {page.PdfPageIndex}", e);
            if (requestedPixelWidth == wanted)
            {
                requestedPixelWidth = 0;
            }
        }
        finally
        {
            if (pendingRender == request)
            {
                pendingRender = null;
            }
            request.Dispose();
        }
    }

    // The bitmap shown; the one shown before is freed.
    private void Show(ImageSource? bitmap)
    {
        var shown = image.Source;
        image.Source = bitmap;
        if (shown != bitmap)
        {
            PictureRenderer.Release(shown);
        }
    }
}
