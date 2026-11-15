using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using Windows.UI.Core;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;
using InkInputProcessingMode = Microsoft.UI.Xaml.Controls.InkInputProcessingMode;
using InkInputRightDragAction = Microsoft.UI.Xaml.Controls.InkInputRightDragAction;

namespace XournalStudio.Views;

/// <summary>
/// An InkToolbar drives a single InkCanvas, but every page has its own. The toolbar targets one of them and this
/// copies the toolbar's tool (pen, highlighter, eraser) to all other pages' InkPresenters. The target follows the
/// page in view (for the stencils, which belong to one presenter).
/// While a custom tool (lasso, shapes, PDF area) is active the presenters take no input, as Microsoft's own samples
/// do for modes without ink (<c>IsInputEnabled = false</c>): pen and mouse then arrive as XAML pointer events for
/// <see cref="InkTools"/>. (The documented route, <c>UnprocessedInput</c>, has unreadable pointer data in the
/// Windows App SDK so far – microsoft-ui-xaml PR #12292.)
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

    // The tool active before the barrel button switched to the lasso.
    private InkToolbarToolButton? toolBeforeLasso;

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

    /// <summary>
    /// Barrel button (or right mouse button) while inking: the presenter leaves that input unprocessed
    /// (<c>RightDragAction.LeaveUnprocessed</c>, as documented for selection) and the toolbar switches to the lasso.
    /// The event's pointer data is not readable yet (PR #12292), only the fact that it happened is used.
    /// </summary>
    public void Attach(PageView view, InkToolbarCustomToolButton lasso)
    {
        view.Ink.InputProcessingConfiguration.RightDragAction = InkInputRightDragAction.LeaveUnprocessed;
        view.Ink.UnprocessedInput.PointerPressed += (_, _) =>
        {
            if (CustomTool == null && toolbar.ActiveTool is InkToolbarToolButton current)
            {
                toolBeforeLasso = current;
                toolbar.ActiveTool = lasso;
            }
        };
    }

    /// <summary>Back to the tool that was active before the barrel button switched to the lasso.</summary>
    public void ReturnFromLasso()
    {
        if (toolBeforeLasso != null)
        {
            toolbar.ActiveTool = toolBeforeLasso;
            toolBeforeLasso = null;
        }
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
        bool custom = CustomTool != null;
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
            if (attributes != null)
            {
                view.Ink.UpdateDefaultDrawingAttributes(attributes);
            }
        }
    }
}
