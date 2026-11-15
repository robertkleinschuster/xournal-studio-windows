using System.Xml.Linq;
using Windows.ApplicationModel.DataTransfer;
using Windows.Foundation;
using Windows.Storage.Streams;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>
/// Copied pictures and groups through the Windows clipboard. Own format: a <see cref="XoppClip"/> (strokes and
/// pictures in points, so pasting here keeps sizes and pressure exactly), plus a bitmap other apps paste. Bitmaps
/// from other apps (e.g. a part of an exercise cut out with the Snipping Tool, Win+Shift+S) paste as a picture.
/// Strokes alone go through Windows ink's own clipboard format (<c>InkStrokeContainer.CopySelectedToClipboard</c>).
/// </summary>
public static class ClipboardService
{
    private const string ClipFormat = "XournalStudio.Clip";

    /// <summary>The group copied by this app, or null.</summary>
    public static async Task<XElement?> GetClipAsync()
    {
        var content = Clipboard.GetContent();
        if (!content.Contains(ClipFormat))
        {
            return null;
        }
        var data = await content.GetDataAsync(ClipFormat);
        if (data is string xml)
        {
            return XElement.Parse(xml);
        }
        // Never silently: e.g. after Clipboard.Flush the string came back as an unreadable IInspectable.
        string kind = data is WinRT.IInspectable i ? i.GetRuntimeClassName() : data?.GetType().FullName ?? "null";
        DebugLog.Write($"own clip format not readable ({kind}); pasting the bitmap instead");
        return null;
    }

    /// <summary>A bitmap on the clipboard as a picture group (screen pixels at 96 dpi as points), or null.</summary>
    public static async Task<XElement?> GetBitmapAsClipAsync()
    {
        var content = Clipboard.GetContent();
        if (!content.Contains(StandardDataFormats.Bitmap))
        {
            return null;
        }
        var reference = await content.GetBitmapAsync();
        using var stream = await reference.OpenReadAsync();
        var (png, width, height) = await PictureRenderer.ToPngAsync(stream);
        var bounds = new Rect(0, 0, width * 0.75, height * 0.75);
        return XoppClip.Create(bounds, [XoppImage.Create(bounds, png, width, height)]);
    }

    /// <summary>
    /// Puts a group on the clipboard, while the app runs, with <paramref name="bitmap"/> (PNG) for other apps. (No
    /// <c>Clipboard.Flush</c>: it is optional, failed with CLIPBRD_E_CANT_OPEN whenever another process – the
    /// clipboard history – opened the clipboard right after <c>SetContent</c>, and after a successful flush our own
    /// format came back as an unreadable IInspectable.)
    /// </summary>
    public static async Task CopyAsync(XElement clip, byte[]? bitmap)
    {
        var package = new DataPackage();
        package.SetData(ClipFormat, clip.ToString(SaveOptions.DisableFormatting));
        if (bitmap != null)
        {
            package.SetBitmap(RandomAccessStreamReference.CreateFromStream(await PictureRenderer.ToStreamAsync(bitmap)));
        }
        Clipboard.SetContent(package);
    }
}
