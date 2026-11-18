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
    private readonly PdfBackground? pdf;
    private readonly Image pdfImage = new() { Stretch = Stretch.Fill };
    private readonly Microsoft.UI.Xaml.Controls.Canvas rulingLayer = new();
    private readonly Microsoft.UI.Xaml.Controls.Canvas imagesLayer = new(); // below the ink, as Xournal layers them
    private readonly Microsoft.UI.Xaml.Controls.Canvas textsLayer = new();
    private readonly Microsoft.UI.Xaml.Controls.Canvas drawnStrokesLayer = new(); // styled strokes Windows ink cannot draw
    private readonly PageBackground background;
    private bool picturesShown;
    private double picturePixelsPerPoint;

    public XoppPage Page { get; }

    /// <summary>The background PDF of a PDF page, otherwise null.</summary>
    public PdfBackground? Pdf => pdf;

    /// <summary>The element showing a picture, while pictures are shown (near the viewport).</summary>
    public Image? FindPicture(XElement picture) =>
        imagesLayer.Children.OfType<Image>().FirstOrDefault(i => i.Tag == picture);

    /// <param name="strokes">The page's strokes, read for this view (<see cref="XoppPage.ReadStrokes"/>).</param>
    public PageView(XoppPage page, PdfBackground? pdf, IEnumerable<(InkStroke Stroke, XElement Element)> strokes)
        : base(page.Width, page.Height)
    {
        Page = page;
        this.pdf = page.IsPdf ? pdf : null;
        foreach (var layer in new UIElement[] { pdfImage, rulingLayer, imagesLayer, textsLayer, drawnStrokesLayer })
        {
            UnderInk.Children.Add(layer);
        }

        SheetBackground = new SolidColorBrush(SheetColor(page));
        if (!page.IsPdf && !page.IsImageBackground)
        {
            foreach (var ruling in BackgroundRuling.For(page.Style, page.BackgroundConfig, page.BackgroundColor, page.Width, page.Height))
            {
                rulingLayer.Children.Add(BackgroundPattern.Create(ruling));
            }
        }

        background = new PageBackground(page, this.pdf, pdfImage);
        page.AttachInk(InkCanvas.InkPresenter.StrokeContainer, strokes);
        page.ImagesChanged += OnImagesChanged;
        page.ImagesMoved += OnImagesMoved;
        FileContentLayers.ShowTexts(page, textsLayer);
        FileContentLayers.ShowDrawnStrokes(page, drawnStrokesLayer);
    }

    /// <summary>The sheet's color below a page's background: its solid color, white below a PDF page or a picture.</summary>
    public static Color SheetColor(XoppPage page) =>
        page.IsPdf || page.IsImageBackground ? Colors.White : page.BackgroundColor;

    /// <summary>
    /// The view is no longer shown: its bitmaps are freed, its waiting render requests dropped, and the page no longer
    /// reaches it. With <paramref name="keepInk"/> (the page stays in the document) the page writes its strokes back
    /// (<see cref="XoppPage.DetachInk"/>).
    /// </summary>
    public void Close(bool keepInk)
    {
        UpdateBackground(false, 0);
        Page.ImagesChanged -= OnImagesChanged;
        Page.ImagesMoved -= OnImagesMoved;
        if (keepInk)
        {
            Page.DetachInk();
        }
    }

    private void OnImagesChanged(object? sender, IReadOnlyCollection<XElement> changed) => PicturesChanged(changed);

    private void OnImagesMoved(object? sender, IReadOnlyCollection<XElement> moved) => MovePictures(moved);

    // ---------------------------------------------------------------- the page's content for the tools (InkPage)

    /// <summary>A rectangle on a PDF page also marks that area: copying takes what is seen there.</summary>
    public override bool KeepsSelectedAreas => pdf != null;

    protected override void OnStrokeReplaced(uint oldId, uint newId) => Page.RenameStroke(oldId, newId);

    public override void BeginStrokePreview(InkStroke stroke, Matrix3x2 committed) => Page.BeginPreview(stroke, committed);

    public override void EndStrokePreview(IEnumerable<InkStroke> strokes) => Page.EndPreview(strokes);

    public override void StrokeTransformed(InkStroke stroke, Matrix3x2 m) => Page.RecordTransform(stroke, m);

    public override object CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<object> pictures,
        Func<InkStroke, Matrix3x2>? transformOf = null) => Page.CapturePlacement(strokes, pictures.Cast<XElement>(), transformOf);

    public override void RestorePlacement(object placement)
    {
        var p = (Placement)placement;
        Page.RestorePlacement(p, CurrentStrokes(p.Strokes.Select(s => s.Stroke)));
    }

    /// <summary>
    /// Adds stroke and picture elements (not recorded yet). <paramref name="skipped"/>: elements that cannot be ink or a
    /// picture here (strokes drawn from the file only, elements this app does not place). If adding fails, what was
    /// added is taken back and the error thrown.
    /// </summary>
    public PageEdit AddElements(IEnumerable<XElement> elements, out int skipped)
    {
        var edit = new PageEdit(this);
        skipped = 0;
        try
        {
            foreach (var e in elements)
            {
                if (e.Name == "stroke")
                {
                    if (Page.AddStrokeElement(e) is { } stroke)
                    {
                        edit.AddedStrokes.Add(stroke);
                    }
                    else
                    {
                        skipped++;
                    }
                }
                else if (XoppImage.IsImage(e))
                {
                    edit.AddedPictures.Add(e); // recorded before attached: a take-back finds it
                }
                else
                {
                    skipped++;
                }
            }
            Page.AddImages(edit.AddedPictures.Cast<XElement>().ToList());
        }
        catch
        {
            TakeBack(edit); // nothing half added; the caller reports the original error
            throw;
        }
        return edit;
    }

    /// <summary>
    /// Takes back what an add that failed or was refused had added: its strokes, and its pictures if they were attached
    /// (nothing else an undo does applies). An error doing so is logged; the original one is what the caller reports.
    /// </summary>
    public void TakeBack(PageEdit edit)
    {
        try
        {
            PageEdit.RemoveStrokes(this, edit.AddedStrokes);
        }
        catch (Exception e)
        {
            DebugLog.Write("taking back the strokes of a failed add", e);
        }
        var attached = edit.AddedPictures.Cast<XElement>().Where(i => i.Parent != null).ToList();
        if (attached.Count > 0)
        {
            try
            {
                Page.RemoveImages(attached);
            }
            catch (Exception e)
            {
                DebugLog.Write("taking back the pictures of a failed add", e);
            }
        }
    }

    /// <summary>
    /// A dragged selection goes on to another page: the file's elements themselves (exact widths, unknown attributes),
    /// transformed into the target's coordinates – all or nothing (null when one of them cannot be placed there; the
    /// app says so on <c>SelectionTool.TransferFailed</c>).
    /// </summary>
    public override (List<InkStroke> Strokes, List<object> Pictures)? CopyTo(InkPage target, IReadOnlyList<InkStroke> strokes,
        IReadOnlyCollection<object> pictures, Matrix3x2 m)
    {
        var to = (PageView)target;
        var elements = pictures.Cast<XElement>().Select(i => XoppClip.Transformed(i, m))
            .Concat(strokes.Select(s => XoppClip.Transformed(Page.ElementOf(s), m))).ToList();
        var edit = to.AddElements(elements, out int skipped);
        if (skipped > 0 || elements.Count == 0)
        {
            DebugLog.Write($"move to page: {skipped} of {elements.Count} element(s) cannot be placed there – nothing moved");
            to.TakeBack(edit);
            return null;
        }
        return (edit.AddedStrokes.ToList(), edit.AddedPictures.ToList());
    }

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
                LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * picturePixelsPerPoint, 1, PageBackground.MaxPixelWidth));
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
            background.Release();
            ClearPictures();
            picturesShown = false;
            return;
        }
        if (!picturesShown || pixelsPerPoint > picturePixelsPerPoint * 1.6)
        {
            ShowPictures(pixelsPerPoint);
        }
        background.Update(pixelsPerPoint);
    }

    // The pictures leave the page; their bitmaps are freed.
    private void ClearPictures()
    {
        foreach (var image in imagesLayer.Children.OfType<Image>())
        {
            PictureRenderer.Release(image.Source);
        }
        imagesLayer.Children.Clear();
    }

    private void ShowPictures(double pixelsPerPoint)
    {
        picturesShown = true;
        picturePixelsPerPoint = pixelsPerPoint;
        ClearPictures();
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
                PictureRenderer.Release(image.Source);
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
        LoadPicture(image, element, (uint)Math.Clamp(bounds.Width * picturePixelsPerPoint, 1, PageBackground.MaxPixelWidth));
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

    // Counts a picture's loads: only the latest one is shown (a decode for an earlier size may finish later).
    private static readonly DependencyProperty LoadNumberProperty =
        DependencyProperty.RegisterAttached("LoadNumber", typeof(int), typeof(PageView), new PropertyMetadata(0));

    // Decodes a picture for this width; the image it replaces is freed (also when it is decoded again after scaling).
    private static async void LoadPicture(Image image, XElement element, uint pixelWidth)
    {
        int load = (int)image.GetValue(LoadNumberProperty) + 1;
        image.SetValue(LoadNumberProperty, load);
        try
        {
            string content = element.Value; // read here (the element belongs to this thread), decoded on the thread pool
            var data = await Task.Run(() => XoppImage.GetData(content));
            var source = await PictureRenderer.LoadAsync(data, pixelWidth, "picture on a page");
            if (image.Parent == null || (int)image.GetValue(LoadNumberProperty) != load)
            {
                PictureRenderer.Release(source); // the picture left the page, or a newer load replaces this one
            }
            else
            {
                var shown = image.Source;
                image.Source = source;
                PictureRenderer.Release(shown);
            }
        }
        catch (Exception e)
        {
            DebugLog.Write("show picture", e);
        }
    }
}
