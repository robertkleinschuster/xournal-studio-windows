using Microsoft.UI.Xaml.Controls;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace InkPages;

/// <summary>
/// The writing area's parts wired together: the <see cref="PenToolbar"/> drives every page's ink
/// (<see cref="ToolController"/>), the custom tools take the pen's unprocessed input (<see cref="InkTools"/>: selection,
/// shapes, partial eraser), the selection is moved as XAML pointer input with auto-scroll, and every change of ink is
/// one step of <see cref="Undo"/>. The toolbar's controls hold the choices (shape, selection mode, eraser): an app sets
/// them before creating the workspace and listens to them to keep its settings.
/// </summary>
public sealed class InkWorkspace
{
    private readonly PenToolbar bar;
    private int generation; // counts Reset: recognition still running for older pages is dropped

    /// <param name="partialErasing">The eraser erases partially (the toolbar's mode toggle checked).</param>
    public InkWorkspace(PenToolbar bar, ScrollView scroller, bool partialErasing = false)
    {
        this.bar = bar;
        Tools = new ToolController(bar.Ink, bar.Pens, bar.Highlighters);
        this.partialErasing = partialErasing;
        Input = new InkTools(Undo, () => Tools.PenAttributes) { SelectionTakenOver = () => Tools.SelectionTakenOver };
        Selection.OtherPageAt = OtherPageAt;

        // A selection dragged across pages: scrolling at the view's edge, dropped on another page it moves there.
        var autoScroll = new DragAutoScroll(scroller);
        Input.AutoScroll = autoScroll;
        // Moving/scaling the selection: XAML pointer input while the pen hovers over it (before any page is attached).
        Input.Drag = new SelectionDrag(Selection, over => Tools.SelectionTakenOver = over) { AutoScroll = autoScroll };
        Selection.DragChangedPage += Input.FollowDrag;
        var commands = new SelectionCommands(bar.Copy, [bar.Cut, bar.Duplicate, bar.DeleteSelection]);
        Selection.SelectionChanged += (_, _) =>
        {
            commands.ShowFor(Selection.Current);
            if (Selection.Current == null)
            {
                Tools.SelectionTakenOver = false; // nothing to grab: the pen inks again even before its next hover
            }
        };

        Input.Shapes.Shape = (ShapeKind)Math.Max(0, bar.ShapeKind.SelectedIndex);
        bar.ShapeKind.SelectionChanged += (_, _) =>
        {
            if (bar.ShapeKind.SelectedIndex >= 0)
            {
                Input.Shapes.Shape = (ShapeKind)bar.ShapeKind.SelectedIndex;
            }
        };
        Selection.Mode = (SelectMode)Math.Max(0, bar.SelectMode.SelectedIndex);
        bar.SelectMode.SelectionChanged += (_, _) =>
        {
            if (bar.SelectMode.SelectedIndex >= 0)
            {
                Selection.Mode = (SelectMode)bar.SelectMode.SelectedIndex;
            }
        };
        Tools.CustomToolChanged += (_, _) => Input.Tool =
            Tools.CustomTool == bar.Select ? InkTool.Select :
            Tools.CustomTool == bar.Shapes ? InkTool.Shape : InkTool.None;

        Eraser = new PartialEraser(Undo, ClearSelectionOf) { Radius = bar.EraserRadius.Value };
        bar.EraserRadius.ValueChanged += (_, e) => Eraser.Radius = e.NewValue;
        Input.EraserToolActive = () => Tools.EraserActive;
        Input.ReleaseEraserEnd = () => Tools.EraserEndTakenOver = false;
        Input.TakeEraserEnd = () => Tools.EraserEndTakenOver = true;
        Input.TakeForSelection = over => Tools.SelectionTakenOver = over;
        ApplyEraserMode();
        // The eraser's mode is the toolbar's toggle (not a tool): the eraser tool and the pen's eraser follow it.
        bar.PartialErase.IsChecked = partialErasing;
        bar.PartialErase.Checked += (_, _) => PartialErasing = true;
        bar.PartialErase.Unchecked += (_, _) => PartialErasing = false;

        Undo.Changed += (_, _) =>
        {
            bar.Undo.IsEnabled = Undo.CanUndo;
            bar.Redo.IsEnabled = Undo.CanRedo;
        };
    }

    public UndoStack Undo { get; } = new();

    public ToolController Tools { get; }

    public InkTools Input { get; }

    public SelectionTool Selection => Input.Selection;

    public PartialEraser Eraser { get; }

    private bool partialErasing;

    /// <summary>The eraser erases exactly the circle at the pen (the toolbar's mode toggle): the tool and the pen's eraser.</summary>
    public bool PartialErasing
    {
        get => partialErasing;
        private set
        {
            if (partialErasing != value)
            {
                partialErasing = value;
                ApplyEraserMode();
                PartialErasingChanged?.Invoke(this, EventArgs.Empty);
            }
        }
    }

    public event EventHandler? PartialErasingChanged;

    private List<InkPage> pages = [];

    /// <summary>
    /// Another page under a point of <paramref name="from"/> (its units), with the offset from its coordinates to the
    /// other page's; null over the page itself or between pages. The pages lie in one panel (as <see cref="SetPages"/>
    /// gives them): one transform into the panel, then each page's layout offset in it – this runs on every pen event
    /// of a drag beside the page.
    /// </summary>
    private (InkPage Page, System.Numerics.Vector2 Offset)? OtherPageAt(InkPage from, Windows.Foundation.Point p)
    {
        if (Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(from) is not Microsoft.UI.Xaml.UIElement panel)
        {
            return null;
        }
        var inPanel = from.InkCanvas.TransformToVisual(panel).TransformPoint(p);
        var inner = from.InkCanvas.TransformToVisual(from).TransformPoint(default); // the same in every page
        foreach (var page in pages)
        {
            if (page == from || Microsoft.UI.Xaml.Media.VisualTreeHelper.GetParent(page) != panel)
            {
                continue;
            }
            var origin = page.ActualOffset;
            double x = inPanel.X - origin.X - inner.X, y = inPanel.Y - origin.Y - inner.Y;
            if (x >= 0 && y >= 0 && x <= page.Width && y <= page.Height)
            {
                return (page, new System.Numerics.Vector2((float)(x - p.X), (float)(y - p.Y)));
            }
        }
        return null;
    }

    /// <summary>Moves a dragged selection's content to another page (see <see cref="SelectionTool.TransferToPage"/>).</summary>
    public Func<Selection, InkPage, System.Numerics.Matrix3x2, (List<InkStroke> Strokes, List<object> Pictures, IPageEdit Edit)?> TransferToPage
    {
        get => Selection.TransferToPage;
        set => Selection.TransferToPage = value;
    }

    /// <summary>Wires a new page: the tools' input, undo of its ink, shape recognition. Once per page.</summary>
    public void Attach(InkPage page)
    {
        Input.Attach(page);
        ToolController.Attach(page);
        page.Ink.StrokesCollected += (_, e) => OnStrokesCollected(page, e.Strokes);
        page.Ink.StrokesErased += (_, e) => OnStrokesErased(page, e.Strokes);
    }

    /// <summary>The pages now shown, in one panel (after attaching, inserting or removing).</summary>
    public void SetPages(IEnumerable<InkPage> shown)
    {
        pages = shown.ToList();
        Tools.SetPages(pages);
    }

    /// <summary>The page in view: the toolbar drives it (the ruler appears where the user writes).</summary>
    public void SetCurrentPage(InkPage? page) => Tools.SetCurrentPage(page);

    /// <summary>Other pages are shown (another document): drags end, no selection, no history.</summary>
    public void Reset()
    {
        generation++;
        Input.CancelGestureOn(_ => true); // a drag on the old pages ends with them
        // The pen inks on the new pages (otherwise its first tip stroke could stay without ink until a hover reaches a
        // page); an eraser end still touching while the pages are replaced is the rare case.
        Input.ReleaseEraserEnd();
        Selection.Clear();
        Undo.Clear();
    }

    /// <summary>
    /// A page is about to be removed: a drag ends if <paramref name="affected"/> says its page is concerned (e.g. pages
    /// that move up under the pen), a selection on the page ends.
    /// </summary>
    public void Removing(InkPage page, Func<InkPage, bool> affected)
    {
        Input.CancelGestureOn(affected);
        if (Selection.Current?.Page == page)
        {
            Selection.Clear(); // a selection on another page stays
        }
    }

    /// <summary>A page was removed: its history goes with it.</summary>
    public void Removed(InkPage page) => Undo.Forget(page);

    /// <summary>
    /// Undoes the last step (the selection ends: undo may replace its strokes). False while the selection is being moved
    /// or scaled: nothing is undone (the app may say "let go first").
    /// </summary>
    public bool UndoStep()
    {
        if (Selection.IsDragging)
        {
            return false;
        }
        Selection.Clear();
        Undo.Undo();
        return true;
    }

    /// <summary>Redoes the last undone step; false (nothing done) while the selection is being moved or scaled.</summary>
    public bool RedoStep()
    {
        if (Selection.IsDragging)
        {
            return false;
        }
        Selection.Clear();
        Undo.Redo();
        return true;
    }

    // The partial eraser (eraser tool and the pen's eraser end/button) or Windows ink's whole-stroke eraser.
    private void ApplyEraserMode()
    {
        bool partial = partialErasing;
        Input.Eraser = partial ? Eraser : null; // first: a running erase ends while the app still has the pen
        Tools.EraserEndTakenOver = false;
        Tools.PartialErasing = partial;
        if (!partial)
        {
            Eraser.Hide();
        }
    }

    // A selection must not keep strokes that are gone (it would copy, move or bring them back).
    private void ClearSelectionOf(IReadOnlyCollection<InkStroke> gone)
    {
        var ids = gone.Select(s => s.Id).ToHashSet();
        if (Selection.Current?.Strokes.Any(s => ids.Contains(s.Id)) == true)
        {
            Selection.Clear();
        }
    }

    private void OnStrokesErased(InkPage page, IReadOnlyList<InkStroke> strokes)
    {
        // Also when they are put back below: as copies, not the selected strokes; the selection ends.
        ClearSelectionOf(strokes.ToList());
        if (Tools.PartialErasing && !Tools.EraserActive)
        {
            // Fallback: the eraser end touched with partial erasing on before its hover handed it to the app
            // (InkTools.TakeEraserEnd), and Windows ink erased whole strokes. They are put back, and from its next press
            // on it goes to the partial eraser until the tip hovers again.
            // (The copies are added on top: InkStrokeContainer has no insert at a position.)
            PageEdit.RestoreStrokes(page, strokes.ToList());
            Tools.EraserEndTakenOver = true;
            return;
        }
        var edit = new PageEdit(page);
        edit.RemovedStrokes.AddRange(strokes);
        Undo.Push(edit);
    }

    /// <summary>Records new ink for undo; with shape recognition on, hand-drawn shapes become clean ones.</summary>
    private async void OnStrokesCollected(InkPage page, IReadOnlyList<InkStroke> strokes)
    {
        int shown = generation;
        var edit = new PageEdit(page);
        try
        {
            foreach (var stroke in strokes.ToList())
            {
                InkStroke? shape = null;
                if (bar.RecognizeShapes.IsChecked == true && !stroke.DrawingAttributes.DrawAsHighlighter)
                {
                    try
                    {
                        shape = await ShapeRecognition.RecognizeAsync(stroke);
                    }
                    catch (Exception e)
                    {
                        InkLog.Write("shape recognition (stroke kept as drawn)", e);
                    }
                    if (generation != shown || page.Parent == null)
                    {
                        return; // other pages are shown meanwhile, or the page was removed: this history is gone
                    }
                }
                var ink = page.Ink.StrokeContainer;
                if (shape != null && ink.GetStrokes().Contains(stroke))
                {
                    PageEdit.RemoveStrokes(page, [stroke]);
                    ink.AddStroke(shape);
                    edit.AddedStrokes.Add(shape);
                }
                else
                {
                    edit.AddedStrokes.Add(stroke);
                }
            }
            Undo.Push(edit);
        }
        catch (Exception e) // async void: logged, then it goes on to the app's unhandled-exception handler
        {
            InkLog.Write("strokes collected", e);
            throw;
        }
    }
}
