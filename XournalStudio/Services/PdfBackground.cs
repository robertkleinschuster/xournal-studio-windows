using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Data.Pdf;
using Windows.Storage;

namespace XournalStudio.Services;

/// <summary>Renders pages of the background PDF with the Windows PDF renderer (Windows.Data.Pdf, see <see cref="PdfRenderer"/>).</summary>
public sealed class PdfBackground
{
    private readonly PdfDocument pdf;

    private PdfBackground(PdfDocument pdf)
    {
        this.pdf = pdf;
    }

    public int PageCount => (int)pdf.PageCount;

    public static async Task<PdfBackground> LoadAsync(string path)
    {
        var file = await StorageFile.GetFileFromPathAsync(Path.GetFullPath(path));
        return new PdfBackground(await PdfDocument.LoadFromFileAsync(file));
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
    /// Renders a page into an image that is <paramref name="pixelWidth"/> pixels wide. UI thread only. Throws
    /// <see cref="OperationCanceledException"/> when <paramref name="cancellation"/> is cancelled before the rendering began.
    /// </summary>
    public async Task<ImageSource?> RenderAsync(int pageIndex, uint pixelWidth, CancellationToken cancellation = default)
    {
        if (pageIndex < 0 || pageIndex >= PageCount)
        {
            return null;
        }
        var time = System.Diagnostics.Stopwatch.StartNew();
        using var page = pdf.GetPage((uint)pageIndex);
        uint pixelHeight = (uint)Math.Max(1, Math.Round(pixelWidth * page.Size.Height / page.Size.Width));
        using var bitmap = await PdfRenderer.Shared.RenderAsync(page, null, pixelWidth, pixelHeight, cancellation);
        var rendered = time.Elapsed;
        var source = new SoftwareBitmapSource();
        await source.SetBitmapAsync(bitmap);
        DebugLog.Write($"pdf page {pageIndex} {pixelWidth}px: rendered {rendered.TotalMilliseconds:F0} ms (incl. waiting), " +
            $"bitmap source {(time.Elapsed - rendered).TotalMilliseconds:F0} ms");
        return source;
    }
}
