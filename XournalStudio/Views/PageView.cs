using System.Xml.Linq;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.UI;
using XournalStudio.Model;
using XournalStudio.Services;
using System.Numerics;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using Matrix = Microsoft.UI.Xaml.Media.Matrix;

namespace XournalStudio.Views;

/// <summary>
/// A page of the document as the library's <see cref="InkPage"/>: background (color, ruling, PDF page or picture),
/// pictures, texts and the strokes drawn from the file below the ink; the page's strokes in the InkCanvas, their file
/// elements and its pictures in <see cref="XoppPage"/>.
/// </summary>
public sealed partial class PageView : InkPage
{
    private const uint MaxPdfPixelWidth = 3200;

    private readonly PdfBackground? pdf;
    private readonly Image pdfImage = new() { Stretch = Stretch.Fill };
    private readonly Microsoft.UI.Xaml.Controls.Canvas rulingLayer = new();
    private readonly Microsoft.UI.Xaml.Controls.Canvas imagesLayer = new(); // below the ink, as Xournal layers them
    private readonly Microsoft.UI.Xaml.Controls.Canvas textsLayer = new();
    private readonly Microsoft.UI.Xaml.Controls.Canvas drawnStrokesLayer = new(); // styled strokes Windows ink cannot draw
    private uint renderedPixelWidth;
    private uint requestedPixelWidth;
    private bool picturesShown;
    private double picturePixelsPerPoint;

    public XoppPage Page { get; }

    /// <summary>The background PDF of a PDF page, otherwise null.</summary>
    public PdfBackground? Pdf => pdf;

    /// <summary>The element showing a picture, while pictures are shown (near the viewport).</summary>
    public Image? FindPicture(XElement picture) =>
        imagesLayer.Children.OfType<Image>().FirstOrDefault(i => i.Tag == picture);

    public PageView(XoppPage page, PdfBackground? pdf) : base(page.Width, page.Height)
    {
        Page = page;
        this.pdf = page.IsPdf ? pdf : null;
        foreach (var layer in new UIElement[] { pdfImage, rulingLayer, imagesLayer, textsLayer, drawnStrokesLayer })
        {
            UnderInk.Children.Add(layer);
        }

        SheetBackground = new SolidColorBrush(page.IsPdf || page.IsImageBackground ? Colors.White : page.BackgroundColor);
        if (!page.IsPdf && !page.IsImageBackground)
        {
            foreach (var ruling in BackgroundRuling.For(page.Style, page.BackgroundConfig, page.BackgroundColor, page.Width, page.Height))
            {
                rulingLayer.Children.Add(BackgroundPattern.Create(ruling));
            }
        }

        page.AttachInk(InkCanvas.InkPresenter.StrokeContainer);
        page.ImagesChanged += (_, changed) => PicturesChanged(changed);
        page.ImagesMoved += (_, moved) => MovePictures(moved);
        ShowTexts();
        ShowDrawnStrokes();
    }

    // ---------------------------------------------------------------- the page's content for the tools (InkPage)

    /// <summary>A rectangle on a PDF page also marks that area: copying takes what is seen there.</summary>
    public override bool KeepsSelectedAreas => pdf != null;

    public override void StrokeReplaced(uint oldId, uint newId) => Page.TransferStrokeIdentity(oldId, newId);

    public override List<InkStroke?> CurrentStrokes(IEnumerable<InkStroke> strokes) => Page.CurrentAll(strokes);

    public override void BeginStrokePreview(InkStroke stroke, Matrix3x2 committed) => Page.BeginPreview(stroke, committed);

    public override void EndStrokePreview(IEnumerable<InkStroke> strokes) => Page.EndPreview(strokes);

    public override void StrokeTransformed(InkStroke stroke, Matrix3x2 m) => Page.RecordTransform(stroke, m);

    public override object CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<object> pictures,
        Func<InkStroke, Matrix3x2>? transformOf = null) => Page.CapturePlacement(strokes, pictures.Cast<XElement>(), transformOf);

    public override void RestorePlacement(object placement) => Page.RestorePlacement((Placement)placement);

    public override IEnumerable<object> Pictures => Page.Images;

    public override Rect PictureBounds(object picture) => XoppImage.GetBounds((XElement)picture);

    public override Action<Matrix3x2> PreviewPictures(IReadOnlyCollection<object> pictures)
    {
        // Found and read once per drag (only the pictures shown near the viewport): each event only sets the matrix of
        // one transform per picture (the drag's end places them anew, MovePictures).
        var shown = new List<(MatrixTransform Transform, Matrix3x2 Placed)>();
        foreach (var element in pictures.Cast<XElement>())
        {
            if (FindPicture(element) is { } image)
            {
                var transform = new MatrixTransform();
                image.RenderTransform = transform;
                shown.Add((transform, XoppImage.GetPlacement(element).Transform));
            }
        }
        return m =>
        {
            foreach (var (transform, placed) in shown)
            {
                transform.Matrix = ToMatrix(placed * m);
            }
        };
    }

    public override void TransformPictures(IReadOnlyCollection<object> pictures, Matrix3x2 m) =>
        Page.TransformImages(pictures.Cast<XElement>().ToList(), m);

    public override void AddPictures(IReadOnlyCollection<object> pictures) => Page.AddImages(pictures.Cast<XElement>().ToList());

    public override object RemovePictures(IReadOnlyCollection<object> pictures) => Page.RemoveImages(pictures.Cast<XElement>().ToList());

    public override void RestorePictures(object removed) => Page.RestoreImages((List<ImagePlace>)removed);

    /// <summary>
    /// The strokes shown from the file (filled, styled or damaged ones, <see cref="StrokeConverter.NeedsOwnDrawing"/>):
    /// their <see cref="StrokeShape.Parts"/>, as the export draws them.
    /// </summary>
    private void ShowDrawnStrokes()
    {
        foreach (var element in Page.DrawnStrokes)
        {
            if (StrokeConverter.ReadShape(element) is not { } shape)
            {
                continue;
            }
            // A filled highlighter upstream is one translucent shape (filling and line drawn opaque into a mask, blended
            // with the filling's alpha). XAML applies opacity per shape (a Canvas' Opacity and BitmapCache did not group
            // them, sandbox 2026-11-16): both get that alpha, the line over the filling comes out a little stronger.
            var color = shape.Highlighter && shape.Fill >= 0
                ? Color.FromArgb((byte)Math.Clamp(shape.Fill, 0, 255), shape.Color.R, shape.Color.G, shape.Color.B)
                : shape.Color;
            var cap = shape.Cap switch
            {
                StrokeCap.Butt => PenLineCap.Flat,
                StrokeCap.Square => PenLineCap.Square,
                _ => PenLineCap.Round,
            };
            foreach (var part in shape.Parts())
            {
                drawnStrokesLayer.Children.Add(part switch
                {
                    StrokeFill fill => new Microsoft.UI.Xaml.Shapes.Polygon
                    {
                        Points = PointsOf(fill.Points),
                        Fill = new SolidColorBrush(Color.FromArgb((byte)Math.Clamp(shape.Fill, 0, 255), color.R, color.G, color.B)),
                        FillRule = FillRule.Nonzero,
                    },
                    StrokeDot dot => Dot(dot, color),
                    StrokeLine line => Line(shape, line, color, cap),
                    _ => throw new InvalidOperationException(part.GetType().Name),
                });
            }
        }
    }

    // A PointCollection belongs to one shape: each part gets its own.
    private static PointCollection PointsOf(IEnumerable<Point> points)
    {
        var collection = new PointCollection();
        foreach (var p in points)
        {
            collection.Add(p);
        }
        return collection;
    }

    private static Microsoft.UI.Xaml.Shapes.Shape Dot(StrokeDot dot, Color color)
    {
        Microsoft.UI.Xaml.Shapes.Shape shape = dot.Square
            ? new Microsoft.UI.Xaml.Shapes.Rectangle()
            : new Microsoft.UI.Xaml.Shapes.Ellipse();
        shape.Width = shape.Height = dot.Size;
        shape.Fill = new SolidColorBrush(color);
        Microsoft.UI.Xaml.Controls.Canvas.SetLeft(shape, dot.Center.X - dot.Size / 2);
        Microsoft.UI.Xaml.Controls.Canvas.SetTop(shape, dot.Center.Y - dot.Size / 2);
        return shape;
    }

    // XAML dash lengths and offset are in multiples of the thickness (StrokeShape.DashesFor).
    private static Microsoft.UI.Xaml.Shapes.Shape Line(StrokeShape shape, StrokeLine line, Color color, PenLineCap cap)
    {
        DoubleCollection? dash = null;
        if (shape.DashesFor(line.Width) is { } dashes)
        {
            dash = new DoubleCollection();
            foreach (var d in dashes)
            {
                dash.Add(d);
            }
        }
        return new Microsoft.UI.Xaml.Shapes.Polyline
        {
            Points = PointsOf(line.Points),
            Stroke = new SolidColorBrush(color),
            StrokeThickness = line.Width,
            StrokeStartLineCap = cap,
            StrokeEndLineCap = cap,
            StrokeDashCap = cap,
            StrokeLineJoin = PenLineJoin.Round,
            StrokeDashArray = dash,
            StrokeDashOffset = dash != null ? line.DashOffset / line.Width : 0,
        };
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
            textsLayer.Children.Add(block);
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
            pdfImage.Source = null;
            CancelPendingRender();
            renderedPixelWidth = requestedPixelWidth = 0;
            backgroundFailed = false; // looked for again when the page comes back (a sync may have brought it)
            backgroundRequested = backgroundShown = 0;
            backgroundRelease++; // a load still running is not shown
            imagesLayer.Children.Clear();
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
    // meanwhile). A picture that cannot be read or decoded is logged and not tried again (the shown one, if any, stays) until
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
            var bitmap = await PictureRenderer.LoadAsync(data, wanted, $"picture background {path}");
            if (release == backgroundRelease && wanted > backgroundShown)
            {
                pdfImage.Source = bitmap;
                backgroundShown = wanted;
                backgroundRequested = Math.Max(backgroundRequested, wanted); // requested ≥ shown (a failure lowers it)
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
        uint wanted = (uint)Math.Min(MaxPdfPixelWidth, Math.Ceiling(Page.Width * pixelsPerPoint));
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
            var bitmap = await pdf.RenderAsync(Page.PdfPageIndex, wanted, request.Token);
            if (requestedPixelWidth == wanted && bitmap != null)
            {
                pdfImage.Source = bitmap;
                renderedPixelWidth = wanted;
            }
            else
            {
                DebugLog.Write($"pdf page {Page.PdfPageIndex} {wanted}px: not shown (no longer wanted)");
            }
        }
        catch (OperationCanceledException) when (request.IsCancellationRequested) // dropped by CancelPendingRender
        {
            DebugLog.Write($"pdf page {Page.PdfPageIndex} {wanted}px: dropped before rendering");
        }
        catch (Exception e) // async void: nothing may escape; logged, the next viewport update tries again
        {
            DebugLog.Write($"render pdf page {Page.PdfPageIndex}", e);
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

    private void ShowPictures(double pixelsPerPoint)
    {
        picturesShown = true;
        picturePixelsPerPoint = pixelsPerPoint;
        imagesLayer.Children.Clear();
        foreach (var element in Page.Images)
        {
            imagesLayer.Children.Add(CreatePicture(element));
        }
    }

    // Pictures were added, removed or put back: only those change (the others keep their decoded bitmaps). The layer
    // holds the page's pictures bottom to top, so each shown one goes to its index among them.
    private void PicturesChanged(IReadOnlyCollection<XElement> changed)
    {
        if (!picturesShown)
        {
            return;
        }
        foreach (var element in changed)
        {
            if (FindPicture(element) is { } image)
            {
                imagesLayer.Children.Remove(image);
            }
        }
        var wanted = changed.ToHashSet();
        int index = 0;
        foreach (var element in Page.Images)
        {
            if (wanted.Contains(element))
            {
                imagesLayer.Children.Insert(index, CreatePicture(element));
            }
            index++;
        }
    }

    private Image CreatePicture(XElement element)
    {
        var bounds = XoppImage.GetBounds(element);
        var image = new Image { Stretch = Stretch.Fill, Tag = element };
        Place(image, element);
        image.SetValue(PlacedWidthProperty, bounds.Width);
        LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * picturePixelsPerPoint, 1, MaxPdfPixelWidth));
        return image;
    }

    // The width on the page a picture was decoded for (to decode again when it is scaled).
    private static readonly DependencyProperty PlacedWidthProperty =
        DependencyProperty.RegisterAttached("PlacedWidth", typeof(double), typeof(PageView), new PropertyMetadata(0.0));

    /// <summary>Places a picture as the file says (see <see cref="XoppImage.GetPlacement"/>), moved by <paramref name="preview"/>.</summary>
    public static void Place(Image image, XElement element, Matrix3x2? preview = null)
    {
        var placement = XoppImage.GetPlacement(element);
        image.Width = placement.Size.Width;
        image.Height = placement.Size.Height;
        image.RenderTransform = new MatrixTransform { Matrix = ToMatrix(placement.Transform * (preview ?? Matrix3x2.Identity)) };
    }

    private static Matrix ToMatrix(Matrix3x2 m) =>
        new() { M11 = m.M11, M12 = m.M12, M21 = m.M21, M22 = m.M22, OffsetX = m.M31, OffsetY = m.M32 };

    private static async void LoadPicture(Image image, XElement element, uint pixelWidth)
    {
        try
        {
            string content = element.Value; // read here (the element belongs to this thread), decoded on the thread pool
            var data = await Task.Run(() => XoppImage.GetData(content));
            image.Source = await PictureRenderer.LoadAsync(data, pixelWidth, "picture on a page");
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
