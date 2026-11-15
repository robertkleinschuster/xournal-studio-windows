using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Core;
using InkInputProcessingMode = Microsoft.UI.Xaml.Controls.InkInputProcessingMode;

namespace XournalStudio.Views;

/// <summary>
/// An InkToolbar drives a single InkCanvas, but every page has its own. The toolbar targets one of them and this
/// copies the toolbar's tool (pen, highlighter, eraser) to all other pages' InkPresenters. The target follows the
/// page in view (for the stencils, which belong to one presenter).
/// </summary>
public sealed class ToolController
{
    // Xournal++'s default palette.
    private static readonly Color[] PenColors =
    [
        Color.FromArgb(255, 0x00, 0x00, 0x00), // black
        Color.FromArgb(255, 0x33, 0x33, 0xcc), // blue
        Color.FromArgb(255, 0xff, 0x00, 0x00), // red
        Color.FromArgb(255, 0x00, 0x80, 0x00), // green
        Color.FromArgb(255, 0x80, 0x80, 0x80), // gray
        Color.FromArgb(255, 0x00, 0xc0, 0xff), // light blue
        Color.FromArgb(255, 0x00, 0xff, 0x00), // light green
        Color.FromArgb(255, 0xff, 0x00, 0xff), // magenta
        Color.FromArgb(255, 0xff, 0x80, 0x00), // orange
        Color.FromArgb(255, 0xff, 0xff, 0x00), // yellow
    ];

    private readonly InkToolbar toolbar;
    private readonly List<PageView> views = new();

    public ToolController(InkToolbar toolbar, InkToolbarBallpointPenButton pen, InkToolbarHighlighterButton highlighter)
    {
        this.toolbar = toolbar;

        pen.Palette = PenColors.Select(c => (Brush)new SolidColorBrush(c)).ToList();
        pen.SelectedBrushIndex = 1;
        // Xournal's pen widths: fine 0.85, medium 1.41, thick 2.26.
        pen.MinStrokeWidth = 0.5;
        pen.MaxStrokeWidth = 6;
        pen.SelectedStrokeWidth = 1.41;
        highlighter.MinStrokeWidth = 2;
        highlighter.MaxStrokeWidth = 30;
        highlighter.SelectedStrokeWidth = 8.5;

        toolbar.ActiveToolChanged += (_, _) => ApplyToAll();
        toolbar.InkDrawingAttributesChanged += (_, _) => ApplyToAll();
    }

    public void SetPages(IEnumerable<PageView> pages)
    {
        views.Clear();
        views.AddRange(pages);
        foreach (var view in views)
        {
            // Pen draws (mouse too, for convenience); touch is left to the ScrollView for panning and zooming.
            view.Ink.InputDeviceTypes = CoreInputDeviceTypes.Pen | CoreInputDeviceTypes.Mouse;
        }
        if (toolbar.TargetInkCanvas == null || !views.Any(v => v.InkCanvas == toolbar.TargetInkCanvas))
        {
            toolbar.TargetInkCanvas = views.FirstOrDefault()?.InkCanvas;
        }
        ApplyToAll();
    }

    /// <summary>
    /// Makes the toolbar drive the page in view, so the Windows ruler/protractor (stencil button) appears where the
    /// user writes.
    /// </summary>
    public void SetCurrentPage(PageView? page)
    {
        if (page != null && toolbar.TargetInkCanvas != page.InkCanvas)
        {
            toolbar.TargetInkCanvas = page.InkCanvas;
        }
    }

    private void ApplyToAll()
    {
        bool erasing = toolbar.ActiveTool is InkToolbarEraserButton;
        var attributes = toolbar.InkDrawingAttributes;
        if (attributes != null)
        {
            // Polyline like Xournal++ and like loaded strokes: what is drawn looks the same after reloading.
            attributes.FitToCurve = false;
        }
        foreach (var view in views)
        {
            view.Ink.InputProcessingConfiguration.Mode = erasing ? InkInputProcessingMode.Erasing : InkInputProcessingMode.Inking;
            if (attributes != null)
            {
                view.Ink.UpdateDefaultDrawingAttributes(attributes);
            }
        }
    }
}
