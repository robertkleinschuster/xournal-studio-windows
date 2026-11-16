using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Core;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using InkInputProcessingMode = Microsoft.UI.Xaml.Controls.InkInputProcessingMode;
using InkInputRightDragAction = Microsoft.UI.Xaml.Controls.InkInputRightDragAction;

namespace XournalStudio.Views;

/// <summary>
/// One InkToolbar drives a target page's InkCanvas and shares its selected drawing settings with every page.
/// Custom tools and barrel-button selection use the presenter's native UnprocessedInput stream.
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
    private readonly InkToolbarHighlighterButton highlighter;
    private readonly List<PageView> views = new();

    /// <summary>The active custom tool button, or null while a pen, the highlighter or the eraser is active.</summary>
    public InkToolbarCustomToolButton? CustomTool => toolbar.ActiveTool as InkToolbarCustomToolButton;

    public event EventHandler? CustomToolChanged;

    public event EventHandler? StrokeWidthsChanged;

    private bool partialErasing;

    /// <summary>
    /// The eraser erases partially (<see cref="PartialEraser"/>): with the eraser tool active the presenters take no
    /// input (as for a custom tool) and the app erases. The pen's eraser end/button is taken on its inverted hover
    /// (<see cref="EraserEndTakenOver"/>). <c>InkInputConfiguration.IsEraserInputEnabled</c> stays at its default: set to
    /// false it did not stop native erasing, and together with the takeover the presenter dropped the eraser pointer
    /// (device 2026-11-16: Exited while in range, the press never arrived; a probe without it got the press).
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
    /// so the eraser end's press reaches the partial eraser; set on its inverted hover (InkTools) – or, as a fallback,
    /// when Windows ink erased with it before (see PagesController) – and cleared when the tip hovers again.
    /// </summary>
    public bool EraserEndTakenOver
    {
        get => eraserEndTakenOver;
        set
        {
            if (eraserEndTakenOver != value)
            {
                eraserEndTakenOver = value;
                ApplyMode();
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

    public ToolController(InkToolbar toolbar, InkToolbarBallpointPenButton pen, InkToolbarHighlighterButton highlighter,
        double penStrokeWidth = 1.41, double highlighterStrokeWidth = 8.5)
    {
        this.toolbar = toolbar;
        this.pen = pen;
        this.highlighter = highlighter;

        pen.Palette = PenColors.Select(c => (Brush)new SolidColorBrush(c)).ToList();
        pen.SelectedBrushIndex = 1;
        // Xournal's pen widths: fine 0.85, medium 1.41, thick 2.26.
        pen.MinStrokeWidth = 0.5;
        pen.MaxStrokeWidth = 6;
        pen.SelectedStrokeWidth = Math.Clamp(penStrokeWidth, pen.MinStrokeWidth, pen.MaxStrokeWidth);
        highlighter.MinStrokeWidth = 2;
        highlighter.MaxStrokeWidth = 30;
        highlighter.SelectedStrokeWidth = Math.Clamp(highlighterStrokeWidth, highlighter.MinStrokeWidth, highlighter.MaxStrokeWidth);

        toolbar.ActiveToolChanged += (_, _) =>
        {
            ApplyToAll();
            CustomToolChanged?.Invoke(this, EventArgs.Empty);
        };
        toolbar.InkDrawingAttributesChanged += (_, _) =>
        {
            ApplyToAll();
            StrokeWidthsChanged?.Invoke(this, EventArgs.Empty);
        };
    }

    public double PenStrokeWidth => pen.SelectedStrokeWidth;

    public double HighlighterStrokeWidth => highlighter.SelectedStrokeWidth;

    /// <summary>Barrel-button input bypasses the presenter (lasso, see the class summary).</summary>
    public static void Attach(PageView view)
    {
        view.Ink.InputProcessingConfiguration.RightDragAction = InkInputRightDragAction.LeaveUnprocessed;
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
            ApplyMode(); // the toolbar may apply its tool's mode to its new target; the app's takeovers decide
        }
    }

    private bool selectionTakenOver;

    /// <summary>
    /// The pen's tip hovers over the selection: the presenters leave its input to the app, so a drag there moves or
    /// scales the selection instead of inking (set and cleared by <see cref="InkTools"/> from the hover).
    /// </summary>
    public bool SelectionTakenOver
    {
        get => selectionTakenOver;
        set
        {
            if (selectionTakenOver != value)
            {
                selectionTakenOver = value;
                ApplyMode();
            }
        }
    }

    // The app takes the pen's input: a custom tool, partial erasing, or a hover-time takeover (eraser end, selection).
    private InkInputProcessingMode Mode =>
        CustomTool != null || selectionTakenOver || (partialErasing && (EraserActive || eraserEndTakenOver))
            ? InkInputProcessingMode.None
            : EraserActive ? InkInputProcessingMode.Erasing : InkInputProcessingMode.Inking;

    /// <summary>
    /// Switches only the processing mode – what a takeover during a hover may change. Rewriting the presenters' other
    /// settings while the pen hovers makes them drop the pointer (device 2026-11-16: the eraser circle flashed, an
    /// Exited followed, the press never arrived).
    /// </summary>
    private void ApplyMode()
    {
        var mode = Mode;
        foreach (var view in views)
        {
            if (view.Ink.InputProcessingConfiguration.Mode != mode)
            {
                view.Ink.InputProcessingConfiguration.Mode = mode;
            }
        }
    }

    private void ApplyToAll()
    {
        bool erasing = EraserActive;
        // Mouse can select or use app tools, but it must never create ink or native eraser strokes.
        var devices = CustomTool != null || (partialErasing && erasing)
            ? CoreInputDeviceTypes.Pen | CoreInputDeviceTypes.Mouse
            : CoreInputDeviceTypes.Pen;
        var attributes = toolbar.InkDrawingAttributes;
        if (attributes != null)
        {
            // Polyline like Xournal++ and like loaded strokes: what is drawn looks the same after reloading.
            attributes.FitToCurve = false;
        }
        foreach (var view in views)
        {
            if (view.Ink.InputDeviceTypes != devices)
            {
                view.Ink.InputDeviceTypes = devices;
            }
            if (attributes != null)
            {
                view.Ink.UpdateDefaultDrawingAttributes(attributes);
            }
        }
        ApplyMode();
    }
}
