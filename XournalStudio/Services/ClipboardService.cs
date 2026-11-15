using System.Xml.Linq;
using Windows.ApplicationModel.DataTransfer;
using Windows.Foundation;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>
/// Pastes a bitmap from the Windows clipboard (e.g. a part of an exercise cut out with the Snipping Tool,
/// Win+Shift+S) as a Xournal &lt;image&gt;.
/// </summary>
public static class ClipboardService
{
    /// <summary>The image to paste (at 0,0, sized as shown on screen), or null if the clipboard has no bitmap.</summary>
    public static async Task<XElement?> PasteImageAsync()
    {
        var content = Clipboard.GetContent();
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
}
