using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Core;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using InkInputProcessingMode = Microsoft.UI.Xaml.Controls.InkInputProcessingMode;
using InkInputRightDragAction = Microsoft.UI.Xaml.Controls.InkInputRightDragAction;

namespace InkPages;

/// <summary>
/// One InkToolbar drives a target page's InkCanvas and shares its selected drawing settings with every page.
/// Custom tools and barrel-button selection use the presenter's native UnprocessedInput stream.
/// </summary>
public sealed class ToolController
{
    private readonly InkToolbar toolbar;
    private readonly IReadOnlyList<InkToolbarPenButton> pens;
    private InkToolbarPenButton pen; // the last active pen preset (shapes take its color and width)
    private readonly InkToolbarPenButton highlighter;
    private readonly List<InkPage> views = new();

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
        Color = (pen.SelectedBrush as SolidColorBrush)?.Color ?? Microsoft.UI.Colors.Black,
        Size = new Windows.Foundation.Size(pen.SelectedStrokeWidth, pen.SelectedStrokeWidth),
    };

    /// <param name="pens">The toolbar's pen presets (colors and widths set by the toolbar).</param>
    public ToolController(InkToolbar toolbar, IReadOnlyList<InkToolbarPenButton> pens,
        InkToolbarPenButton highlighter, double highlighterStrokeWidth = 8.5)
    {
        this.toolbar = toolbar;
        this.pens = pens;
        pen = pens[0];
        this.highlighter = highlighter;
        highlighter.SelectedStrokeWidth = Math.Clamp(highlighterStrokeWidth, highlighter.MinStrokeWidth, highlighter.MaxStrokeWidth);

        toolbar.ActiveToolChanged += (_, _) =>
        {
            if (toolbar.ActiveTool is InkToolbarCustomPenButton active && pens.Contains(active))
            {
                pen = active;
            }
            ApplyToAll();
            CustomToolChanged?.Invoke(this, EventArgs.Empty);
        };
        toolbar.InkDrawingAttributesChanged += (_, _) =>
        {
            ApplyToAll();
            StrokeWidthsChanged?.Invoke(this, EventArgs.Empty);
        };
    }

    /// <summary>The pen presets' colors (palette index) and widths, in their order.</summary>
    public IEnumerable<(int ColorIndex, double Width)> PenSettings =>
        pens.Select(p => (p.SelectedBrushIndex, p.SelectedStrokeWidth));

    public double HighlighterStrokeWidth => highlighter.SelectedStrokeWidth;

    /// <summary>Barrel-button input bypasses the presenter (lasso, see the class summary).</summary>
    public static void Attach(InkPage view)
    {
        view.Ink.InputProcessingConfiguration.RightDragAction = InkInputRightDragAction.LeaveUnprocessed;
    }

    public void SetPages(IEnumerable<InkPage> pages)
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
    public void SetCurrentPage(InkPage? page)
    {
        if (page != null && toolbar.TargetInkCanvas != page.InkCanvas)
        {
            toolbar.TargetInkCanvas = page.InkCanvas;
            ApplyMode(); // the toolbar may apply its tool's mode to its new target; the app's takeovers decide
        }
    }

    private bool selectionTakenOver;

    /// <summary>
    /// The pen hovers over the selection: the presenters take no pen input, so the pen's press and drag there arrive as
    /// XAML pointer input (<see cref="SelectionDrag"/>: moving or scaling is an app interaction, not ink). Set from the
    /// hover by <see cref="InkTools"/> (presenter side) and <see cref="SelectionDrag"/> (XAML side).
    /// </summary>
    public bool SelectionTakenOver
    {
        get => selectionTakenOver;
        set
        {
            if (selectionTakenOver != value)
            {
                selectionTakenOver = value;
                ApplyDevices();
            }
        }
    }

    // The app takes the pen's input: a custom tool, partial erasing, or the eraser end's hover-time takeover.
    private InkInputProcessingMode Mode =>
        CustomTool != null || (partialErasing && (EraserActive || eraserEndTakenOver))
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

    // Mouse can select or use app tools, but it must never create ink or native eraser strokes. Over the selection the
    // presenters take no input at all (it goes to XAML).
    private CoreInputDeviceTypes Devices =>
        selectionTakenOver ? CoreInputDeviceTypes.None
        : CustomTool != null || (partialErasing && EraserActive) ? CoreInputDeviceTypes.Pen | CoreInputDeviceTypes.Mouse
        : CoreInputDeviceTypes.Pen;

    private void ApplyDevices()
    {
        var devices = Devices;
        foreach (var view in views)
        {
            if (view.Ink.InputDeviceTypes != devices)
            {
                view.Ink.InputDeviceTypes = devices;
            }
        }
    }

    private void ApplyToAll()
    {
        var attributes = toolbar.InkDrawingAttributes;
        if (attributes != null)
        {
            // Polyline like Xournal++ and like loaded strokes: what is drawn looks the same after reloading.
            attributes.FitToCurve = false;
        }
        foreach (var view in views)
        {
            if (attributes != null)
            {
                view.Ink.UpdateDefaultDrawingAttributes(attributes);
            }
        }
        ApplyDevices();
        ApplyMode();
    }
}
