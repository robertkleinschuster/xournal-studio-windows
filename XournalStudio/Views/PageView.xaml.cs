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
        Sheet.Background = new SolidColorBrush(page.IsPdf || page.IsImageBackground ? Colors.White : page.BackgroundColor);
        if (!page.IsPdf && !page.IsImageBackground)
        {
            foreach (var ruling in BackgroundRuling.For(page.Style, page.BackgroundConfig, page.BackgroundColor, page.Width, page.Height))
            {
                RulingLayer.Children.Add(BackgroundPattern.Create(ruling));
            }
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
        ShowTexts();
        ShowDrawnStrokes();
    }

    /// <summary>Filled, dashed or butt/square-ended strokes as upstream draws them: the filling, then the line.</summary>
    private void ShowDrawnStrokes()
    {
        foreach (var element in Page.DrawnStrokes)
        {
            if (StrokeConverter.ReadShape(element) is not { } shape)
            {
                continue;
            }
            // A PointCollection belongs to one shape: the filling and the line get their own.
            PointCollection Points(IEnumerable<Point> from)
            {
                var collection = new PointCollection();
                foreach (var p in from)
                {
                    collection.Add(p);
                }
                return collection;
            }
            // A filled highlighter upstream is one translucent shape (filling and line drawn opaque into a mask, blended
            // with the filling's alpha). XAML applies opacity per shape (a Canvas' Opacity and BitmapCache did not group
            // them, sandbox 2026-11-16): both get that alpha, the line over the filling comes out a little stronger.
            byte highlighterAlpha = (byte)Math.Clamp(shape.Fill, 0, 255);
            var color = shape.Highlighter && shape.Fill >= 0
                ? Color.FromArgb(highlighterAlpha, shape.Color.R, shape.Color.G, shape.Color.B)
                : shape.Color;
            if (shape.Fill >= 0)
            {
                DrawnStrokesLayer.Children.Add(new Microsoft.UI.Xaml.Shapes.Polygon
                {
                    Points = Points(shape.Points),
                    Fill = new SolidColorBrush(Color.FromArgb((byte)Math.Clamp(shape.Fill, 0, 255), color.R, color.G, color.B)),
                    FillRule = FillRule.Nonzero,
                });
            }
            var cap = shape.Cap switch
            {
                StrokeCap.Butt => PenLineCap.Flat,
                StrokeCap.Square => PenLineCap.Square,
                _ => PenLineCap.Round,
            };
            // XAML dash lengths are in multiples of the thickness; the file's are in points.
            DoubleCollection? Dash(double width)
            {
                if (shape.Dashes is not { Length: > 0 } dashes || width <= 0)
                {
                    return null;
                }
                var collection = new DoubleCollection();
                foreach (var d in dashes)
                {
                    collection.Add(d / width);
                }
                return collection;
            }
            // offset = the length already drawn: the dashes run on over the segments of a stroke with pressure.
            Microsoft.UI.Xaml.Shapes.Shape Line(PointCollection pts, double width, double offset = 0) => new Microsoft.UI.Xaml.Shapes.Polyline
            {
                StrokeDashOffset = width > 0 ? offset / width : 0,
                Points = pts,
                Stroke = new SolidColorBrush(color),
                StrokeThickness = width,
                StrokeStartLineCap = cap,
                StrokeEndLineCap = cap,
                StrokeDashCap = cap,
                StrokeLineJoin = PenLineJoin.Round,
                StrokeDashArray = Dash(width),
            };
            if (shape.EvenWidth)
            {
                DrawnStrokesLayer.Children.Add(Line(Points(shape.Points), shape.Width));
            }
            else
            {
                double drawn = 0;
                for (int i = 0; i + 1 < shape.Points.Count; i++)
                {
                    var (a, b) = (shape.Points[i], shape.Points[i + 1]);
                    DrawnStrokesLayer.Children.Add(Line(Points([a, b]), shape.SegmentWidths[i], drawn));
                    drawn += Math.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y));
                }
            }
        }
    }

    /// <summary>The file's texts (and links) as TextBlocks: font, size, color, wrap width, alignment and placement as stored.</summary>
    private void ShowTexts()
    {
        foreach (var element in Page.Texts)
        {
            var text = XoppText.Read(element);
            var block = new TextBlock
            {
                Text = text.Content,
                FontFamily = new FontFamily(text.WindowsFamily),
                FontSize = text.Size, // points, as the page's units
                FontWeight = text.Bold ? Microsoft.UI.Text.FontWeights.Bold : Microsoft.UI.Text.FontWeights.Normal,
                FontStyle = text.Italic ? Windows.UI.Text.FontStyle.Italic : Windows.UI.Text.FontStyle.Normal,
                Foreground = new SolidColorBrush(text.Color),
                IsTextScaleFactorEnabled = false, // the page's size, not the system's text size
                TextAlignment = text.Justify ? TextAlignment.Justify : text.Alignment switch
                {
                    XoppTextAlignment.Center => TextAlignment.Center,
                    XoppTextAlignment.Right => TextAlignment.Right,
                    _ => TextAlignment.Left,
                },
            };
            if (text.Wrap is { } wrap)
            {
                block.Width = wrap;
                block.TextWrapping = TextWrapping.Wrap;
            }
            var m = text.Transform;
            block.RenderTransform = new MatrixTransform
            {
                Matrix = new Matrix { M11 = m.M11, M12 = m.M12, M21 = m.M21, M22 = m.M22, OffsetX = m.M31, OffsetY = m.M32 },
            };
            TextsLayer.Children.Add(block);
        }
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
            bool resized = Math.Abs(bounds.Width - (double)image.GetValue(PlacedWidthProperty)) > 0.5;
            Place(image, element); // also ends the selection's live preview
            image.SetValue(PlacedWidthProperty, bounds.Width);
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
            backgroundFailed = false; // looked for again when the page comes back (a sync may have brought it)
            backgroundRequested = backgroundShown = 0;
            backgroundRelease++; // a load still running is not shown
            ImagesLayer.Children.Clear();
            picturesShown = false;
            return;
        }
        if (!picturesShown || pixelsPerPoint > picturePixelsPerPoint * 1.6)
        {
            ShowPictures(pixelsPerPoint);
        }
        if (Page.IsImageBackground)
        {
            ShowImageBackground(pixelsPerPoint);
        }
        RenderPdf(pixelsPerPoint);
    }

    // A picture background, stretched over the page as upstream draws it; decoded at about the shown size while the
    // page is near the viewport (freed with the PDF bitmaps otherwise), again when zoomed in a lot. The file is read
    // into memory off the UI thread (not held open: a sync or rename is not blocked) and decoded like the pictures
    // (PictureRenderer); a decoded bitmap is shown when it is sharper than the one shown (the previous one stays
    // meanwhile). A picture that cannot be read or decoded while nothing is shown is logged and not tried again until
    // the page leaves the viewport and comes back.
    private bool backgroundFailed;
    private uint backgroundRequested, backgroundShown; // pixel widths
    private int backgroundRelease; // counts the page's releases: loads started before one are not shown

    private async void ShowImageBackground(double pixelsPerPoint)
    {
        string? path = Page.BackgroundImagePath;
        uint wanted = (uint)Math.Min(MaxPdfPixelWidth, Math.Ceiling(Page.Width * pixelsPerPoint));
        if (path == null || backgroundFailed || backgroundRequested * 1.6 >= wanted)
        {
            return;
        }
        backgroundRequested = wanted; // not started again on every viewport update
        int release = backgroundRelease;
        try
        {
            byte[] data = await Task.Run(() => File.ReadAllBytes(path));
            var bitmap = await PictureRenderer.LoadAsync(data, wanted);
            if (release == backgroundRelease && wanted > backgroundShown)
            {
                PdfImage.Source = bitmap;
                backgroundShown = wanted;
            }
        }
        catch (Exception e) // any failure of reading or decoding (async void: nothing may escape); logged
        {
            DebugLog.Write($"picture background {path}", e);
            if (release == backgroundRelease && wanted == backgroundRequested) // not a load a newer one replaced
            {
                backgroundFailed = true; // a sharper decode failed: the shown one (if any) stays, not retried
                backgroundRequested = backgroundShown;
            }
        }
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
            var image = new Image { Stretch = Stretch.Fill, Tag = element };
            Place(image, element);
            image.SetValue(PlacedWidthProperty, bounds.Width);
            ImagesLayer.Children.Add(image);
            LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * pixelsPerPoint, 1, MaxPdfPixelWidth));
        }
    }

    // The width on the page a picture was decoded for (to decode again when it is scaled).
    private static readonly DependencyProperty PlacedWidthProperty =
        DependencyProperty.RegisterAttached("PlacedWidth", typeof(double), typeof(PageView), new PropertyMetadata(0.0));

    /// <summary>Places a picture as the file says (see <see cref="XoppImage.GetPlacement"/>), moved by <paramref name="preview"/>.</summary>
    public static void Place(Image image, XElement element, System.Numerics.Matrix3x2? preview = null)
    {
        var placement = XoppImage.GetPlacement(element);
        image.Width = placement.Size.Width;
        image.Height = placement.Size.Height;
        var m = placement.Transform * (preview ?? System.Numerics.Matrix3x2.Identity);
        image.RenderTransform = new MatrixTransform
        {
            Matrix = new Matrix { M11 = m.M11, M12 = m.M12, M21 = m.M21, M22 = m.M22, OffsetX = m.M31, OffsetY = m.M32 },
        };
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

/// <summary>A page's ruling lines of one color and width (<see cref="RulingLines"/>) as a XAML Path.</summary>
internal static class BackgroundPattern
{
    public static Microsoft.UI.Xaml.Shapes.Path Create(RulingLines ruling)
    {
        var group = new GeometryGroup();
        foreach (var (from, to) in ruling.Lines)
        {
            group.Children.Add(new LineGeometry { StartPoint = from, EndPoint = to });
        }
        double r = ruling.Width / 2;
        foreach (var dot in ruling.Dots)
        {
            group.Children.Add(new EllipseGeometry { Center = dot, RadiusX = r, RadiusY = r });
        }
        var brush = new SolidColorBrush(ruling.Color);
        return new Microsoft.UI.Xaml.Shapes.Path
        {
            Data = group,
            Stroke = ruling.Lines.Count > 0 ? brush : null,
            StrokeThickness = ruling.Width,
            Fill = ruling.Dots.Count > 0 ? brush : null,
        };
    }
}
