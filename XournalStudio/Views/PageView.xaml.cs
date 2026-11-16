using System.Xml.Linq;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.UI;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// A page: background (color, ruling or PDF page), pictures and the InkCanvas.
/// </summary>
public sealed partial class PageView : UserControl
{
    private const uint MaxPdfPixelWidth = 3200;

    private readonly PdfBackground? pdf;
    private uint renderedPixelWidth;
    private uint requestedPixelWidth;
    private bool picturesShown;
    private double picturePixelsPerPoint;

    public XoppPage Page { get; }

    public InkPresenter Ink => Canvas.InkPresenter;

    public InkCanvas InkCanvas => Canvas;

    /// <summary>The page area; pointer input of the custom tools arrives here.</summary>
    public UIElement Surface => Sheet;

    /// <summary>Layer above the ink for tool feedback (lasso, selection, previews), in page points.</summary>
    public Microsoft.UI.Xaml.Controls.Canvas Overlay => ToolLayer;

    /// <summary>The background PDF of a PDF page, otherwise null.</summary>
    public PdfBackground? Pdf => pdf;

    /// <summary>The element showing a picture, while pictures are shown (near the viewport).</summary>
    public Image? FindPicture(XElement picture) =>
        ImagesLayer.Children.OfType<Image>().FirstOrDefault(i => i.Tag == picture);

    public PageView(XoppPage page, PdfBackground? pdf)
    {
        InitializeComponent();
        Page = page;
        this.pdf = page.IsPdf ? pdf : null;

        Width = page.Width;
        Height = page.Height;
        Sheet.Background = new SolidColorBrush(page.IsPdf ? Colors.White : page.BackgroundColor);
        if (!page.IsPdf)
        {
            Pattern.Data = BackgroundPattern.Create(page.Style, page.Width, page.Height, out Color lineColor);
            Pattern.Stroke = new SolidColorBrush(lineColor);
            Pattern.StrokeThickness = Ruling.LineWidth;
        }

        page.AttachInk(Canvas.InkPresenter.StrokeContainer);
        page.ImagesChanged += (_, _) =>
        {
            if (picturesShown)
            {
                ShowPictures(picturePixelsPerPoint);
            }
        };
        page.ImagesMoved += (_, moved) => MovePictures(moved);
    }

    // Only the moved pictures change: new place and size; decoded again only when the size changed.
    private void MovePictures(IReadOnlyCollection<XElement> moved)
    {
        foreach (var element in moved)
        {
            if (FindPicture(element) is not { } image)
            {
                continue;
            }
            var bounds = XoppImage.GetBounds(element);
            bool resized = Math.Abs(bounds.Width - image.Width) > 0.5;
            image.RenderTransform = null; // the selection's live preview
            Microsoft.UI.Xaml.Controls.Canvas.SetLeft(image, bounds.Left);
            Microsoft.UI.Xaml.Controls.Canvas.SetTop(image, bounds.Top);
            image.Width = bounds.Width;
            image.Height = bounds.Height;
            if (resized)
            {
                LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * picturePixelsPerPoint, 1, MaxPdfPixelWidth));
            }
        }
    }

    /// <summary>
    /// Renders the PDF page and the pictures sharp enough for <paramref name="pixelsPerPoint"/> while the page is
    /// near the viewport and frees the bitmaps otherwise.
    /// </summary>
    public void UpdateBackground(bool nearViewport, double pixelsPerPoint)
    {
        if (!nearViewport)
        {
            PdfImage.Source = null;
            renderedPixelWidth = requestedPixelWidth = 0;
            ImagesLayer.Children.Clear();
            picturesShown = false;
            return;
        }
        if (!picturesShown || pixelsPerPoint > picturePixelsPerPoint * 1.6)
        {
            ShowPictures(pixelsPerPoint);
        }
        RenderPdf(pixelsPerPoint);
    }

    private async void RenderPdf(double pixelsPerPoint)
    {
        if (pdf == null)
        {
            return;
        }
        uint wanted = (uint)Math.Min(MaxPdfPixelWidth, Math.Ceiling(Page.Width * pixelsPerPoint));
        bool sharpEnough = renderedPixelWidth >= wanted * 0.9 && renderedPixelWidth <= wanted * 1.6;
        if (sharpEnough || requestedPixelWidth == wanted)
        {
            return;
        }

        requestedPixelWidth = wanted;
        var bitmap = await pdf.RenderAsync(Page.PdfPageIndex, wanted);
        if (requestedPixelWidth == wanted && bitmap != null)
        {
            PdfImage.Source = bitmap;
            renderedPixelWidth = wanted;
        }
    }

    private void ShowPictures(double pixelsPerPoint)
    {
        picturesShown = true;
        picturePixelsPerPoint = pixelsPerPoint;
        ImagesLayer.Children.Clear();
        foreach (var element in Page.Images)
        {
            var bounds = XoppImage.GetBounds(element);
            var image = new Image { Width = bounds.Width, Height = bounds.Height, Stretch = Stretch.Fill, Tag = element };
            Microsoft.UI.Xaml.Controls.Canvas.SetLeft(image, bounds.Left);
            Microsoft.UI.Xaml.Controls.Canvas.SetTop(image, bounds.Top);
            ImagesLayer.Children.Add(image);
            LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * pixelsPerPoint, 1, MaxPdfPixelWidth));
        }
    }

    private static async void LoadPicture(Image image, XElement element, uint pixelWidth)
    {
        try
        {
            image.Source = await PictureRenderer.LoadAsync(XoppImage.GetData(element), pixelWidth);
        }
        catch (Exception e)
        {
            DebugLog.Write("show picture", e);
        }
    }
}

/// <summary>A page's ruling (<see cref="BackgroundRuling"/>) as XAML geometry.</summary>
internal static class BackgroundPattern
{
    public static Geometry? Create(BackgroundStyle style, double width, double height, out Color color)
    {
        var ruling = BackgroundRuling.For(style, width, height);
        color = ruling?.Color ?? Colors.Transparent;
        if (ruling == null)
        {
            return null;
        }
        var group = new GeometryGroup();
        foreach (var (from, to) in ruling.Lines)
        {
            group.Children.Add(new LineGeometry { StartPoint = from, EndPoint = to });
        }
        foreach (var dot in ruling.Dots)
        {
            group.Children.Add(new EllipseGeometry { Center = dot, RadiusX = Ruling.DotRadius, RadiusY = Ruling.DotRadius });
        }
        return group;
    }
}
