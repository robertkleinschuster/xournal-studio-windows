using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace InkPages;

/// <summary>
/// A page to write on: the app's layers (background, pictures, anything drawn below the ink) in
/// <see cref="UnderInk"/>, the <see cref="InkCanvas"/>, and an overlay for the tools' feedback – all in page units
/// (the page's size is its <see cref="FrameworkElement.Width"/> and <see cref="FrameworkElement.Height"/>).
/// <para>
/// The ink is the canvas' own <c>InkStrokeContainer</c>. What the app keeps beside it – its own data per stroke
/// (e.g. the file's element of a stroke) and the pictures – the tools reach through the members an app implements here:
/// pictures are the app's objects (compared by reference), placements and removed pictures are tokens the app gives
/// out and takes back.
/// </para>
/// </summary>
public abstract partial class InkPage : UserControl
{
    private readonly Grid sheet;

    protected InkPage(double width, double height)
    {
        Width = width;
        Height = height;
        InkCanvas = new InkCanvas();
        Overlay = new XamlCanvas { IsHitTestVisible = false };
        UnderInk = new Grid { IsHitTestVisible = false };
        sheet = Themed.Load<Grid>("Grid", "BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='0.5'");
        sheet.Children.Add(UnderInk);
        sheet.Children.Add(InkCanvas);
        sheet.Children.Add(Overlay);
        Content = sheet;
    }

    /// <summary>
    /// A blank sheet of a page's size and look, without ink or content: stands in for a page whose view is not created
    /// (an app that shows only the pages near the viewport), so the layout and the scroll extent stay the same.
    /// </summary>
    public static FrameworkElement CreatePlaceholder(double width, double height, Brush? background)
    {
        var sheet = Themed.Load<Border>("Border", "BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='0.5'");
        sheet.Width = width;
        sheet.Height = height;
        sheet.Background = background; // hit-test visible like a page: touch on it pans the view
        return sheet;
    }

    public InkCanvas InkCanvas { get; }

    public Microsoft.UI.Xaml.Controls.InkPresenter Ink => InkCanvas.InkPresenter;

    /// <summary>Layers below the ink, filled by the app (not hit-test visible).</summary>
    public Grid UnderInk { get; }

    /// <summary>Layer above the ink for tool feedback (lasso, selection, previews), in page units.</summary>
    public XamlCanvas Overlay { get; }

    /// <summary>Receives the pen as XAML pointer input while the presenters leave it (moving the selection).</summary>
    public UIElement Surface => sheet;

    /// <summary>The sheet's color.</summary>
    public Brush? SheetBackground
    {
        get => sheet.Background;
        set => sheet.Background = value;
    }

    /// <summary>A rectangle selection on this page also keeps its area (e.g. to copy what is seen there).</summary>
    public virtual bool KeepsSelectedAreas => false;

    // ---------------------------------------------------------------- strokes

    /// <summary>A copy of a stroke took its place (undo/redo put back a removed stroke): the app's data goes with it.</summary>
    public abstract void StrokeReplaced(uint oldId, uint newId);

    /// <summary>The strokes on the page now for strokes an edit remembers: each itself or its copy; null where gone.</summary>
    public abstract List<InkStroke?> CurrentStrokes(IEnumerable<InkStroke> strokes);

    /// <summary>
    /// A move or scale is previewed on the stroke (its <c>PointTransform</c> shows it): until it ends the app treats
    /// the stroke as having <paramref name="committed"/> (a snapshot never holds a half-done move).
    /// </summary>
    public abstract void BeginStrokePreview(InkStroke stroke, Matrix3x2 committed);

    /// <summary>The preview of these strokes ended (committed or reverted).</summary>
    public abstract void EndStrokePreview(IEnumerable<InkStroke> strokes);

    /// <summary>The ink shows the stroke transformed by <paramref name="m"/> (committed): the app's data follows.</summary>
    public abstract void StrokeTransformed(InkStroke stroke, Matrix3x2 m);

    /// <summary>
    /// Where strokes and pictures are now (a token for <see cref="RestorePlacement"/>): undo/redo of a move put back
    /// exactly this. <paramref name="transformOf"/> gives a stroke's transform instead of its current one.
    /// </summary>
    public abstract object CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<object> pictures,
        Func<InkStroke, Matrix3x2>? transformOf = null);

    public abstract void RestorePlacement(object placement);

    // ---------------------------------------------------------------- pictures

    /// <summary>The page's pictures, bottom to top.</summary>
    public abstract IEnumerable<object> Pictures { get; }

    public abstract Rect PictureBounds(object picture);

    /// <summary>Starts a live preview of pictures: the action shows them moved by a transform (identity: as they are).</summary>
    public abstract Action<Matrix3x2> PreviewPictures(IReadOnlyCollection<object> pictures);

    /// <summary>Moves/scales pictures for good (also ends their preview).</summary>
    public abstract void TransformPictures(IReadOnlyCollection<object> pictures, Matrix3x2 m);

    public abstract void AddPictures(IReadOnlyCollection<object> pictures);

    /// <summary>Removes pictures; returns a token for <see cref="RestorePictures"/> (puts them back at their place).</summary>
    public abstract object RemovePictures(IReadOnlyCollection<object> pictures);

    public abstract void RestorePictures(object removed);
}
