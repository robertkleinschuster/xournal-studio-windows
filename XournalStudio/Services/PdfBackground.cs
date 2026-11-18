using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Data.Pdf;
using Windows.Storage;

namespace XournalStudio.Services;

/// <summary>Renders pages of the background PDF with the Windows PDF renderer (Windows.Data.Pdf, see <see cref="PdfRenderer"/>).</summary>
public sealed class PdfBackground : IDisposable
{
    private readonly PdfDocument pdf;
    private readonly Windows.Storage.Streams.IRandomAccessStream stream; // read by the renderer: lives as long as this
    private readonly long size; // the PDF in native memory: told to the GC (it does not see it otherwise)
    private bool disposed;

    private PdfBackground(PdfDocument pdf, Windows.Storage.Streams.IRandomAccessStream stream, long size)
    {
        this.pdf = pdf;
        this.stream = stream;
        this.size = size;
        if (size > 0)
        {
            GC.AddMemoryPressure(size);
        }
    }

    // A PDF still in use when nobody disposes it (the document's: renders may still run when it is replaced) is freed by
    // the GC, which knows its size.
    ~PdfBackground() => Free();

    /// <summary>
    /// Frees the PDF in memory now. Only for a PDF used in one place (a dialog, an import, a thumbnail): never for the
    /// document's, whose page renders may still be running.
    /// </summary>
    public void Dispose()
    {
        stream.Dispose();
        Free();
        GC.SuppressFinalize(this);
    }

    private void Free()
    {
        if (!disposed && size > 0)
        {
            GC.RemoveMemoryPressure(size);
        }
        disposed = true;
    }

    private static async Task<PdfBackground> LoadAsync(Windows.Storage.Streams.IRandomAccessStream stream, long nativeSize)
    {
        try
        {
            return new PdfBackground(await PdfDocument.LoadFromStreamAsync(stream), stream, nativeSize);
        }
        catch
        {
            stream.Dispose(); // e.g. a password or a damaged file: the copy goes at once (the exception goes on)
            throw;
        }
    }

    public int PageCount => (int)pdf.PageCount;

    /// <summary>
    /// A PDF file, read into memory first: a <see cref="PdfDocument"/> loaded from the file keeps it open as long as it lives
    /// (it has no Close), so the open document's PDF could not be deleted (sandbox 2026-11-18: 0x80070005 even after the
    /// document was left). From memory the file is free for deleting, renaming and syncing.
    /// </summary>
    public static async Task<PdfBackground> LoadAsync(string path)
    {
        // Copied into a native in-memory stream (no managed array of the PDF's size on the large-object heap).
        var file = await StorageFile.GetFileFromPathAsync(Path.GetFullPath(path));
        var memory = new Windows.Storage.Streams.InMemoryRandomAccessStream();
        using (var input = await file.OpenReadAsync())
        {
            await Windows.Storage.Streams.RandomAccessStream.CopyAsync(input, memory);
        }
        memory.Seek(0);
        return await LoadAsync(memory, (long)memory.Size);
    }

    /// <summary>A PDF in memory (written by <see cref="PdfExport"/> into a stream).</summary>
    public static async Task<PdfBackground> LoadAsync(byte[] data)
    {
        // A managed array: the GC sees it already.
        return await LoadAsync(new MemoryStream(data, writable: false).AsRandomAccessStream(), 0);
    }

    /// <summary>Page sizes in points (1/72"); the PDF API reports DIPs (1/96").</summary>
    public IReadOnlyList<(double Width, double Height)> GetPageSizes()
    {
        var sizes = new List<(double, double)>();
        for (uint i = 0; i < pdf.PageCount; i++)
        {
            using var page = pdf.GetPage(i);
            sizes.Add((page.Size.Width * 72 / 96, page.Size.Height * 72 / 96));
        }
        return sizes;
    }

    /// <summary>
    /// Renders an area of a page (in points) as PNG with <paramref name="pixelsPerPoint"/>: on white, or only what the page
    /// draws when <paramref name="transparent"/>.
    /// </summary>
    public async Task<(byte[] Png, int Width, int Height)> RenderAreaAsync(int pageIndex, Windows.Foundation.Rect area,
        double pixelsPerPoint, bool transparent = false)
    {
        using var page = pdf.GetPage((uint)pageIndex);
        const double DipsPerPoint = 96.0 / 72;
        var source = new Windows.Foundation.Rect(area.X * DipsPerPoint, area.Y * DipsPerPoint,
            area.Width * DipsPerPoint, area.Height * DipsPerPoint);
        using var bitmap = await PdfRenderer.Shared.RenderAsync(page, source,
            (uint)Math.Max(1, Math.Round(area.Width * pixelsPerPoint)),
            (uint)Math.Max(1, Math.Round(area.Height * pixelsPerPoint)), transparent: transparent);
        return (await PictureRenderer.ToPngAsync(bitmap), bitmap.PixelWidth, bitmap.PixelHeight);
    }

    /// <summary>
    /// Renders a page into an image that is <paramref name="pixelWidth"/> pixels wide (on white, or only what the page
    /// draws when <paramref name="transparent"/>). UI thread only. Throws <see cref="OperationCanceledException"/> when
    /// <paramref name="cancellation"/> is cancelled before the rendering began.
    /// </summary>
    public async Task<ImageSource?> RenderAsync(int pageIndex, uint pixelWidth, CancellationToken cancellation = default,
        bool transparent = false)
    {
        if (pageIndex < 0 || pageIndex >= PageCount)
        {
            return null;
        }
        var time = System.Diagnostics.Stopwatch.StartNew();
        using var page = pdf.GetPage((uint)pageIndex);
        uint pixelHeight = (uint)Math.Max(1, Math.Round(pixelWidth * page.Size.Height / page.Size.Width));
        // Not disposed: the SoftwareBitmapSource keeps the bitmap and copies it again whenever its surfaces were released
        // (out of view, recycled, device lost) – on a worker thread, where a closed bitmap fails fast (RO_E_CLOSED, the
        // 0xc000027b crashes of 2026-11-18). Closing the source closes it: the caller releases a source it does not show
        // any more (PictureRenderer.Release) – the bitmap is native memory the GC does not see.
        var bitmap = await PdfRenderer.Shared.RenderAsync(page, null, pixelWidth, pixelHeight, cancellation, transparent);
        var rendered = time.Elapsed;
        var source = new SoftwareBitmapSource();
        try
        {
            await source.SetBitmapAsync(bitmap);
        }
        catch
        {
            source.Dispose();
            bitmap.Dispose();
            throw;
        }
        DebugLog.Write($"pdf page {pageIndex} {pixelWidth}px: rendered {rendered.TotalMilliseconds:F0} ms (incl. waiting), " +
            $"bitmap source {(time.Elapsed - rendered).TotalMilliseconds:F0} ms");
        return source;
    }
}
