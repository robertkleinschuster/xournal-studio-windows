using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Data.Pdf;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>Decodes picture data of .xopp files: PNG/JPEG, or a one-page PDF (copied areas, LaTeX).</summary>
public static class PictureRenderer
{
    /// <param name="what">Names the picture in the log.</param>
    public static async Task<ImageSource> LoadAsync(byte[] data, uint pixelWidth, string what)
    {
        using var stream = await ToStreamAsync(data);
        if (XoppImage.IsPdf(data))
        {
            var pdf = await PdfDocument.LoadFromStreamAsync(stream);
            using var page = pdf.GetPage(0);
            uint pixelHeight = (uint)Math.Max(1, Math.Round(pixelWidth * page.Size.Height / page.Size.Width));
            // Not disposed: the source keeps it and copies it again later (see PdfBackground.RenderAsync).
            var rendered = await PdfRenderer.Shared.RenderAsync(page, null, pixelWidth, pixelHeight);
            var source = new SoftwareBitmapSource();
            await source.SetBitmapAsync(rendered);
            return source;
        }
        var bitmap = new BitmapImage();
        // Failures while decoding throw from SetSourceAsync; one reported later (when drawn) comes here.
        bitmap.ImageFailed += (_, e) => DebugLog.Write($"{what}: ImageFailed {e.ErrorMessage}");
        // Decode at about the size shown; large screenshots would otherwise stay in memory at full size.
        bitmap.DecodePixelWidth = (int)pixelWidth;
        bitmap.DecodePixelType = DecodePixelType.Physical;
        await bitmap.SetSourceAsync(stream);
        return bitmap;
    }

    /// <summary>Encodes a bitmap as PNG.</summary>
    public static async Task<byte[]> ToPngAsync(SoftwareBitmap bitmap)
    {
        using var output = new InMemoryRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, output);
        // PNG stores straight alpha: a premultiplied bitmap (renders, decoded pictures) is converted by Windows first, so
        // the translucent edges of a transparent render do not come out dark.
        using var straight = bitmap.BitmapAlphaMode == BitmapAlphaMode.Straight
            ? null : SoftwareBitmap.Convert(bitmap, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Straight);
        encoder.SetSoftwareBitmap(straight ?? bitmap);
        await encoder.FlushAsync();
        return await ReadAllAsync(output);
    }

    /// <summary>Re-encodes any bitmap Windows can decode (clipboard DIB, JPEG, ...) as PNG.</summary>
    public static async Task<(byte[] Png, int Width, int Height)> ToPngAsync(IRandomAccessStream input)
    {
        var decoder = await BitmapDecoder.CreateAsync(input);
        using var bitmap = await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
        return (await ToPngAsync(bitmap), bitmap.PixelWidth, bitmap.PixelHeight);
    }

    public static async Task<byte[]> ReadAllAsync(IRandomAccessStream stream)
    {
        stream.Seek(0);
        var bytes = new byte[stream.Size];
        using var reader = new DataReader(stream.GetInputStreamAt(0));
        await reader.LoadAsync((uint)stream.Size);
        reader.ReadBytes(bytes);
        return bytes;
    }

    public static async Task<InMemoryRandomAccessStream> ToStreamAsync(byte[] data)
    {
        var stream = new InMemoryRandomAccessStream();
        using (var writer = new DataWriter(stream.GetOutputStreamAt(0)))
        {
            writer.WriteBytes(data);
            await writer.StoreAsync();
            await writer.FlushAsync();
        }
        stream.Seek(0);
        return stream;
    }
}
