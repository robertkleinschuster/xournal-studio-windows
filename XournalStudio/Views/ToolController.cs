using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Core;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using InkInputProcessingMode = Microsoft.UI.Xaml.Controls.InkInputProcessingMode;

namespace XournalStudio.Views;

/// <summary>
/// An InkToolbar drives a single InkCanvas, but every page has its own. The toolbar targets one of them and this
/// copies the toolbar's tool (pen, highlighter, eraser) to all other pages' InkPresenters. The target follows the
/// page in view (for the stencils, which belong to one presenter).
/// While a custom tool (lasso, shapes, PDF area) is active the presenters take no input, as Microsoft's own samples
/// do for modes without ink (<c>IsInputEnabled = false</c>): pen and mouse then arrive as XAML pointer events for
/// <see cref="InkTools"/>. (The documented route, <c>UnprocessedInput</c>, has unreadable pointer data in the
/// Windows App SDK so far – microsoft-ui-xaml PR #12292.)
/// The presenters ignore input with the pen's barrel button held (documented
/// <c>InkInputConfiguration.IsPrimaryBarrelButtonInputEnabled = false</c>, Microsoft's "barrel button" sample): it then
/// arrives as XAML pointer events with readable data and <see cref="InkTools"/> lassos with it, whatever tool is active.
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
    private readonly InkToolbarBallpointPenButton pen;
    private readonly List<PageView> views = new();

    /// <summary>The active custom tool button, or null while a pen, the highlighter or the eraser is active.</summary>
    public InkToolbarCustomToolButton? CustomTool => toolbar.ActiveTool as InkToolbarCustomToolButton;

    public event EventHandler? CustomToolChanged;

    private bool partialErasing;

    /// <summary>
    /// The eraser erases partially (<see cref="PartialEraser"/>): with the eraser tool active the presenters take no
    /// input (as for a custom tool) and the app erases. The pen's eraser end should come to the app the same way
    /// (documented <c>InkInputConfiguration.IsEraserInputEnabled = false</c>), but 2.6.6-experimental ignores that
    /// setting and erases whole strokes natively (sandbox 2026-11-16: the value reads back false, StrokesErased fires).
    /// It stays set: an SDK that honors it hands the eraser end to the partial eraser without a change here.
    /// </summary>
    public bool PartialErasing
    {
        get => partialErasing;
        set
        {
            partialErasing = value;
            ApplyToAll();
        }
    }

    private bool eraserEndTakenOver;

    /// <summary>
    /// While the pen is turned over to its eraser end (partial erasing, pen tool active), the presenters take no input,
    /// so the eraser end's next press reaches the partial eraser; set when Windows ink erased with it (see PagesController), cleared
    /// when the tip hovers again – before it touches, so it inks as usual.
    /// </summary>
    public bool EraserEndTakenOver
    {
        get => eraserEndTakenOver;
        set
        {
            if (eraserEndTakenOver != value)
            {
                eraserEndTakenOver = value;
                ApplyToAll();
            }
        }
    }

    /// <summary>The toolbar's eraser is the active tool.</summary>
    public bool EraserActive => toolbar.ActiveTool is InkToolbarEraserButton;

    /// <summary>Color and width of the pen button, for shapes.</summary>
    public InkDrawingAttributes PenAttributes => new()
    {
        Color = (pen.SelectedBrush as SolidColorBrush)?.Color ?? PenColors[1],
        Size = new Windows.Foundation.Size(pen.SelectedStrokeWidth, pen.SelectedStrokeWidth),
    };

    public ToolController(InkToolbar toolbar, InkToolbarBallpointPenButton pen, InkToolbarHighlighterButton highlighter)
    {
        this.toolbar = toolbar;
        this.pen = pen;

        pen.Palette = PenColors.Select(c => (Brush)new SolidColorBrush(c)).ToList();
        pen.SelectedBrushIndex = 1;
        // Xournal's pen widths: fine 0.85, medium 1.41, thick 2.26.
        pen.MinStrokeWidth = 0.5;
        pen.MaxStrokeWidth = 6;
        pen.SelectedStrokeWidth = 1.41;
        highlighter.MinStrokeWidth = 2;
        highlighter.MaxStrokeWidth = 30;
        highlighter.SelectedStrokeWidth = 8.5;

        toolbar.ActiveToolChanged += (_, _) =>
        {
            ApplyToAll();
            CustomToolChanged?.Invoke(this, EventArgs.Empty);
        };
        toolbar.InkDrawingAttributesChanged += (_, _) => ApplyToAll();
    }

    /// <summary>Barrel-button input bypasses the presenter (lasso, see the class summary).</summary>
    public static void Attach(PageView view)
    {
        view.Ink.InputConfiguration.IsPrimaryBarrelButtonInputEnabled = false;
    }

    public void SetPages(IEnumerable<PageView> pages)
    {
        views.Clear();
        views.AddRange(pages);
        foreach (var view in views)
        {
            // Only the pen draws; touch is left to the ScrollView for panning and zooming.
            view.Ink.InputDeviceTypes = CoreInputDeviceTypes.Pen;
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
        bool erasing = EraserActive;
        bool custom = CustomTool != null || (partialErasing && (erasing || eraserEndTakenOver)); // the app takes the input
        var attributes = toolbar.InkDrawingAttributes;
        if (attributes != null)
        {
            // Polyline like Xournal++ and like loaded strokes: what is drawn looks the same after reloading.
            attributes.FitToCurve = false;
        }
        foreach (var view in views)
        {
            view.Ink.InputProcessingConfiguration.Mode =
                custom ? InkInputProcessingMode.None : erasing ? InkInputProcessingMode.Erasing : InkInputProcessingMode.Inking;
            view.Ink.IsInputEnabled = !custom;
            view.Ink.InputConfiguration.IsEraserInputEnabled = !partialErasing;
            if (attributes != null)
            {
                view.Ink.UpdateDefaultDrawingAttributes(attributes);
            }
        }
    }
}
