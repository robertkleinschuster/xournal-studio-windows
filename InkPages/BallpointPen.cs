using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using PenTipShape = Windows.UI.Input.Inking.PenTipShape;

namespace InkPages;

/// <summary>
/// A ballpoint pen as an <see cref="InkToolbarCustomPen"/> (round tip, pressure, polyline): InkToolbar holds one
/// built-in ballpoint button only, so pen presets are custom pen buttons – Microsoft's documented way to add pens
/// ("Add an InkToolbar to a Windows app – custom pen"). The flyout's width slider gives steps; the pen draws them with
/// <see cref="PenWidths"/> (thinner than the slider's whole numbers allow).
/// </summary>
public sealed partial class BallpointPen : InkToolbarCustomPen
{
    protected override InkDrawingAttributes CreateInkDrawingAttributesCore(Brush brush, double strokeWidth) => new()
    {
        Color = (brush as SolidColorBrush)?.Color ?? Microsoft.UI.Colors.Black,
        PenTip = PenTipShape.Circle,
        // A step's width is Xournal's: the width at full pressure (see InkStrokes.FullPressureFactor).
        Size = new Size(PenWidths.Of(strokeWidth) / InkStrokes.FullPressureFactor, PenWidths.Of(strokeWidth) / InkStrokes.FullPressureFactor),
        IgnorePressure = false,
        FitToCurve = false,
    };
}

/// <summary>
/// A highlighter as an <see cref="InkToolbarCustomPen"/> (round tip, no pressure, drawn as highlighter): a custom pen
/// like the presets, so it can stand after them (InkToolbar puts its built-in highlighter before custom pens).
/// </summary>
public sealed partial class HighlighterPen : InkToolbarCustomPen
{
    protected override InkDrawingAttributes CreateInkDrawingAttributesCore(Brush brush, double strokeWidth) => new()
    {
        Color = (brush as SolidColorBrush)?.Color ?? Microsoft.UI.Colors.Yellow,
        PenTip = PenTipShape.Circle,
        Size = new Size(strokeWidth, strokeWidth),
        IgnorePressure = true,
        FitToCurve = false,
        DrawAsHighlighter = true,
    };
}
