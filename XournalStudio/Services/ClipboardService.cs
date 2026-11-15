using System.Xml.Linq;
using Windows.ApplicationModel.DataTransfer;
using Windows.Foundation;
using Windows.Storage.Streams;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>
/// Pictures through the Windows clipboard: pastes a bitmap (e.g. a part of an exercise cut out with the Snipping
/// Tool, Win+Shift+S) as a Xournal &lt;image&gt;, and copies an area of a PDF page as a bitmap that other apps can
/// paste too.
/// </summary>
public static class ClipboardService
{
    // Our own format next to the bitmap: the picture as an <image> element, so pasting it here keeps its size in
    // points (a bitmap alone only has pixels).
    private const string ImageFormat = "XournalStudio.Image";

    /// <summary>True if the clipboard holds a picture copied by this app (preferred over ink and bitmaps).</summary>
    public static bool HasPicture() => Clipboard.GetContent().Contains(ImageFormat);

    /// <summary>The image to paste (at 0,0), or null if the clipboard has no bitmap.</summary>
    public static async Task<XElement?> PasteImageAsync()
    {
        var content = Clipboard.GetContent();
        if (content.Contains(ImageFormat))
        {
            var data = await content.GetDataAsync(ImageFormat);
            if (data is string xml)
            {
                return XElement.Parse(xml);
            }
            // Never silently: e.g. after Clipboard.Flush the string came back as an unreadable IInspectable.
            string kind = data is WinRT.IInspectable i ? i.GetRuntimeClassName() : data?.GetType().FullName ?? "null";
            DebugLog.Write($"own picture format not readable ({kind}); pasting the bitmap instead");
        }
        if (!content.Contains(StandardDataFormats.Bitmap))
        {
            return null;
        }
        var reference = await content.GetBitmapAsync();
        using var stream = await reference.OpenReadAsync();
        var (png, width, height) = await PictureRenderer.ToPngAsync(stream);
        // Screen pixels at 96 dpi -> points.
        return XoppImage.Create(new Rect(0, 0, width * 0.75, height * 0.75), png, width, height);
    }

    /// <summary>
    /// Puts a picture of <paramref name="size"/> points on the clipboard, while the app runs. (No
    /// <c>Clipboard.Flush</c>: it is optional, failed with CLIPBRD_E_CANT_OPEN whenever another process – the
    /// clipboard history – opened the clipboard right after <c>SetContent</c>, and after a successful flush our own
    /// format came back as an unreadable IInspectable.)
    /// </summary>
    public static async Task CopyImageAsync(byte[] png, int pixelWidth, int pixelHeight, Size size)
    {
        var stream = await PictureRenderer.ToStreamAsync(png);
        var package = new DataPackage();
        package.SetBitmap(RandomAccessStreamReference.CreateFromStream(stream));
        package.SetData(ImageFormat,
            XoppImage.Create(new Rect(0, 0, size.Width, size.Height), png, pixelWidth, pixelHeight).ToString());
        Clipboard.SetContent(package);
    }
}
