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
    public static async Task<ImageSource> LoadAsync(byte[] data, uint pixelWidth)
    {
        using var stream = await ToStreamAsync(data);
        var bitmap = new BitmapImage();
        // Failures while decoding throw from SetSourceAsync; one reported later (when drawn) comes here.
        bitmap.ImageFailed += (_, e) => DebugLog.Write($"picture: ImageFailed {e.ErrorMessage}");
        if (XoppImage.IsPdf(data))
        {
            var pdf = await PdfDocument.LoadFromStreamAsync(stream);
            using var page = pdf.GetPage(0);
            using var rendered = new InMemoryRandomAccessStream();
            await page.RenderToStreamAsync(rendered, new PdfPageRenderOptions { DestinationWidth = pixelWidth });
            rendered.Seek(0);
            await bitmap.SetSourceAsync(rendered);
        }
        else
        {
            // Decode at about the size shown; large screenshots would otherwise stay in memory at full size.
            bitmap.DecodePixelWidth = (int)pixelWidth;
            bitmap.DecodePixelType = DecodePixelType.Physical;
            await bitmap.SetSourceAsync(stream);
        }
        return bitmap;
    }

    /// <summary>Re-encodes any bitmap Windows can decode (clipboard DIB, JPEG, ...) as PNG.</summary>
    public static async Task<(byte[] Png, int Width, int Height)> ToPngAsync(IRandomAccessStream input)
    {
        var decoder = await BitmapDecoder.CreateAsync(input);
        using var bitmap = await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
        using var output = new InMemoryRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, output);
        encoder.SetSoftwareBitmap(bitmap);
        await encoder.FlushAsync();
        return (await ReadAllAsync(output), bitmap.PixelWidth, bitmap.PixelHeight);
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
