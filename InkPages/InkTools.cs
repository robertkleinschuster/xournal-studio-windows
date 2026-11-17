using CorePointerEventArgs = Windows.UI.Core.PointerEventArgs;
using Windows.Foundation;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;

namespace InkPages;

public enum InkTool
{
    None,
    Select,
    Shape,
    PartialEraser, // only as the tool of a drag: partial erasing is a mode of the eraser, not a tool of its own
}

/// <summary>
/// Pointer input of the custom tools, after Microsoft's pattern for ink selection ("Pen interactions and Windows Ink"):
/// pen/mouse input on the page goes to the active tool (<see cref="SelectionTool"/>, <see cref="ShapeTool"/>), which
/// shows feedback on the page's overlay and leaves the work to the ink's own APIs. Touch is left to the ScrollView.
/// All of it is the presenters' <c>UnprocessedInput</c>: a custom tool sets their processing mode to None, the barrel
/// button is left unprocessed (<c>RightDragAction.LeaveUnprocessed</c>), and hovers – reported also while the presenters
/// ink – hand the pen to the app before it touches: the eraser end with partial erasing, the tip over the selection.
/// </summary>
public sealed class InkTools(UndoStack undo, Func<InkDrawingAttributes> pen)
{
    // The drag in progress: its tool, its page and its pointer – a gesture belongs to its pointer, another one (a palm,
    // a finger beside the pen) neither moves, finishes nor cancels it.
    private sealed record Running(InkTool Tool, InkPage Page, uint Pointer);

    private InkTool tool;
    private Running? running;
    private PartialEraser? eraser;
    private Point last;

    public SelectionTool Selection { get; } = new(undo);

    public ShapeTool Shapes { get; } = new(undo, pen);

    /// <summary>Set while erasing is partial (see <see cref="ToolController.PartialErasing"/>).</summary>
    public PartialEraser? Eraser
    {
        get => eraser;
        set
        {
            if (value != eraser && running?.Tool == InkTool.PartialEraser)
            {
                CancelGestureOn(_ => true); // the running erase ends with the eraser it began with
            }
            eraser = value;
        }
    }

    /// <summary>True while the toolbar's eraser is the active tool.</summary>
    public Func<bool> EraserToolActive { get; set; } = () => false;

    /// <summary>The pen's tip (or the mouse) is back: the presenters take ink input again.</summary>
    public Action ReleaseEraserEnd { get; set; } = () => { };

    /// <summary>The pen's eraser end hovers while erasing is partial: the presenters leave its input to the app.</summary>
    public Action TakeEraserEnd { get; set; } = () => { };

    /// <summary>The dragged selection went on to another page (SelectionTool.DragChangedPage): the drag follows it.</summary>
    public void FollowDrag(InkPage page, Point p)
    {
        Drag?.FollowPage(page); // a drag by XAML pointer input (it acts only while it runs)
        if (running is { Tool: InkTool.Select } r)
        {
            running = r with { Page = page };
            last = p;
            AutoScroll?.FollowPage(page);
        }
    }

    // The page of a XAML selection drag (the selection's page).
    private InkPage? DragPage => Selection.Current?.Page;

    /// <summary>Moving and scaling the selection as XAML pointer input (set by InkWorkspace; see SelectionDrag).</summary>
    public SelectionDrag? Drag { get; set; }

    /// <summary>Scrolls the pages while a selection drag holds the pen near the view's edge (set by InkWorkspace).</summary>
    public DragAutoScroll? AutoScroll { get; set; }

    /// <summary>The pen's tip hovers over the selection (true) or away from it (false): see ToolController.SelectionTakenOver.</summary>
    public Action<bool> TakeForSelection { get; set; } = _ => { };

    public InkTool Tool
    {
        get => tool;
        set
        {
            CancelGestureOn(_ => true);
            if (value != InkTool.Select)
            {
                Selection.Clear();
            }
            tool = value;
        }
    }

    /// <summary>Handles the presenter's native unprocessed input stream (once per page view).</summary>
    public void Attach(InkPage view)
    {
        Drag?.Attach(view);
        var input = view.Ink.UnprocessedInput;
        input.PointerPressed += (_, e) => Logged("Pressed", () => OnPressed(view, e));
        input.PointerMoved += (_, e) => Logged("Moved", () => OnMoved(view, e));
        input.PointerReleased += (_, e) => Logged("Released", () => OnReleased(view, e));
        input.PointerEntered += (_, e) => Logged("Entered", () => OnMoved(view, e));
        input.PointerHovered += (_, e) => Logged("Hovered", () => OnHover(view, e));
        input.PointerExited += (_, e) => Logged("Exited", () =>
        {
            eraser?.Hide();
            if (running == null)
            {
                Hand(false); // off the page: the pen inks wherever it comes back
            }
            EndIfLifted(e.CurrentPoint, e);
        });
        input.PointerLost += (_, e) => Logged("Lost", () =>
        {
            if (Owns(view, e))
            {
                Finish(last);
            }
        });
    }

    /// <summary>The pen is taken over for the selection now (the presenters take no pen input; see ToolController).</summary>
    public Func<bool> SelectionTakenOver { get; set; } = () => false;

    // The presenters' hover hands the pen to the selection; giving it back is XAML's job (SelectionDrag: its hover off
    // the selection, its exit, the drag's end). While the selection has the pen the presenters take no pen input, so
    // their events are stale: they come from WinUI's ink thread, forwarded later, and a late Exited or Hovered gave the
    // pen back to the presenters, which took it from the drag (sandbox 2026-11-17: PointerCaptureLost 60 ms after the
    // press). Rewriting the presenters' devices during a hover also makes them report Exited (see ToolController).
    private void Hand(bool toSelection)
    {
        if (Drag is { IsRunning: true } || (!toSelection && SelectionTakenOver()))
        {
            if (toSelection != SelectionTakenOver())
            {
                InkLog.Write($"ink input: hover would hand the pen {(toSelection ? "to the selection" : "to the ink")} - kept (stale)");
            }
            return;
        }
        TakeForSelection(toSelection);
    }

    // WinUI raises these events from its ink thread's forwarding: an exception would not reach App.UnhandledException,
    // so it is logged here before it propagates (never swallowed).
    private static void Logged(string name, Action handler)
    {
        try
        {
            handler();
        }
        catch (Exception ex)
        {
            InkLog.Write($"unprocessed {name}", ex);
            throw;
        }
    }

    // The running gesture is on this page and of this pointer.
    private bool Owns(InkPage view, CorePointerEventArgs e) =>
        running is { } r && r.Page == view && r.Pointer == e.CurrentPoint.PointerId;

    // The running gesture's pen, on whichever page it is now (a drag may cross pages; see LocalPosition).
    private bool OwnsPointer(CorePointerEventArgs e) => running is { } r && r.Pointer == e.CurrentPoint.PointerId;

    // A hover, exit, or release may be the first event to report a lifted pointer; finish at its last local point.
    private bool EndIfLifted(Windows.UI.Input.PointerPoint point, CorePointerEventArgs e)
    {
        if (running is not { } r || r.Pointer != point.PointerId || IsDown(point))
        {
            return false;
        }
        Finish(last);
        return true;
    }

    private void OnPressed(InkPage view, CorePointerEventArgs e)
    {
        var point = e.CurrentPoint;
        if (running != null || IsTouch(point))
        {
            return;
        }
        bool isPen = IsPen(point);
        bool eraserEnd = IsEraserEnd(point);
        if (!eraserEnd)
        {
            ReleaseEraserEnd(); // the tip or the mouse presses: it inks
        }
        // The pen's barrel button may arrive as barrel or as right button (Lenovo pens).
        bool barrel = isPen && (point.Properties.IsBarrelButtonPressed || point.Properties.IsRightButtonPressed);
        bool down = IsDown(point);
        var active = barrel ? InkTool.Select : tool;
        if (!barrel && Erases(isPen, eraserEnd)) // the barrel always lassos
        {
            active = InkTool.PartialEraser;
        }
        else if (eraserEnd)
        {
            return; // the eraser end erases natively
        }
        else if ((active is InkTool.None or InkTool.Shape) && Selection.Grabs(view, LocalPosition(view, point)))
        {
            active = InkTool.Select; // on the selection the tip moves or scales it, with every tool (hover takeover)
        }
        if (active == InkTool.None || !down)
        {
            return;
        }
        last = LocalPosition(view, point);
        if (active == InkTool.PartialEraser)
        {
            eraser!.Begin(view, last);
        }
        else if (active == InkTool.Select)
        {
            Selection.Begin(view, last, barrel ? SelectMode.Lasso : Selection.Mode); // barrel: always the lasso (user)
        }
        else
        {
            Shapes.Begin(view, last);
        }
        Start(active, view, point.PointerId, e);
    }

    private void OnMoved(InkPage view, CorePointerEventArgs e)
    {
        if (running == null)
        {
            OnHover(view, e);
            return;
        }
        var point = e.CurrentPoint;
        if (EndIfLifted(point, e) || !OwnsPointer(e))
        {
            return; // (a hovering pen never moves, lassos or erases)
        }
        // Moving or scaling a selection needs only the newest position: each preview sets every selected stroke's
        // transform and places the frame (device 2026-11-16: per sample it kept the UI thread busy). A lasso, a shape and
        // the eraser follow the whole path – the eraser in one pass per event (it looks at every stroke of the page).
        if (running.Tool == InkTool.Select && Selection.IsDragging)
        {
            MoveTo(view, point);
        }
        else
        {
            var samples = e.GetIntermediatePoints();
            var path = samples.Count == 0
                ? [LocalPosition(view, point)]
                // Oldest first: the platform does not document the order of the ink events' history.
                : samples.Where(s => s.PointerId == point.PointerId).OrderBy(s => s.Timestamp)
                    .Select(s => LocalPosition(view, s)).ToList();
            if (path.Count > 0)
            {
                MoveAlong(path);
            }
        }
        TrackAutoScroll();
        e.Handled = true;
    }

    private void MoveTo(InkPage view, Windows.UI.Input.PointerPoint point) => MoveAlong([LocalPosition(view, point)]);

    // The running gesture follows the pen along these points (its page's coordinates, oldest first).
    private void MoveAlong(List<Point> path)
    {
        last = path[^1];
        switch (running?.Tool)
        {
            case InkTool.PartialEraser:
                eraser!.MoveAlong(path);
                break;
            case InkTool.Select:
                path.ForEach(Selection.Move);
                break;
            case InkTool.Shape:
                Shapes.Move(last);
                break;
        }
    }

    // Near the view's edge the pages scroll during a selection gesture; the drag follows the content under the pen.
    private void TrackAutoScroll()
    {
        if (running is { Tool: InkTool.Select } r)
        {
            AutoScroll?.Track(r.Page, last, followScroll ??= p =>
            {
                if (running is { Tool: InkTool.Select })
                {
                    last = p;
                    Selection.Move(p);
                }
            });
        }
    }

    private Action<Point>? followScroll;

    // No gesture: hovering shows the eraser's circle (and the tip hovering gives the pen back to the ink). Nothing begins
    // here – every gesture begins with a press on the page (the eraser end is taken on its inverted hover; if Windows ink
    // erased with it first, InkWorkspace puts the strokes back and takes it for its next press).
    private void OnHover(InkPage view, CorePointerEventArgs e)
    {
        var point = e.CurrentPoint;
        if (IsTouch(point))
        {
            return;
        }
        bool isPen = IsPen(point);
        bool eraserEnd = IsEraserEnd(point);
        var position = LocalPosition(view, point);
        // The pen reports which end it hovers with before it touches (unprocessed input carries IsInverted): the eraser
        // end is taken before its first touch, the tip given back before it inks. Only in range – a pen leaving range
        // is no hover of either end.
        if (isPen && point.Properties.IsInRange)
        {
            if (!eraserEnd)
            {
                ReleaseEraserEnd(); // the pen's tip hovers: it will ink (a mouse moving gives nothing back)
                // Over the selection the tip moves or scales it; elsewhere it inks.
                Hand(Selection.Grabs(view, position));
            }
            else
            {
                Hand(false); // the eraser end erases, also over the selection (natively or partially)
                if (eraser != null)
                {
                    TakeEraserEnd(); // erasing is partial: the eraser end's touches go to the partial eraser
                }
            }
        }
        if (!IsDown(point) && Erases(isPen, eraserEnd)) // the circle while hovering
        {
            eraser!.Hover(view, position);
        }
        else
        {
            eraser?.Hide();
        }
    }

    private void Start(InkTool active, InkPage view, uint pointerId, CorePointerEventArgs e)
    {
        InkLog.Write($"ink input: {active} begins (pointer {pointerId})");
        running = new Running(active, view, pointerId);
        e.Handled = true;
    }

    private void OnReleased(InkPage view, CorePointerEventArgs e)
    {
        var point = e.CurrentPoint;
        if (!OwnsPointer(e))
        {
            return; // another pointer's release
        }
        Finish(LocalPosition(view, point));
        e.Handled = true;
    }

    /// <summary>Completes the drag at <paramref name="end"/> (released, or the pointer capture was lost).</summary>
    private void Finish(Point end)
    {
        if (Take() is not { } finished)
        {
            return;
        }
        InkLog.Write($"ink input: {finished.Tool} ends");
        switch (finished.Tool)
        {
            case InkTool.PartialEraser:
                eraser!.End();
                break;
            case InkTool.Select:
                Selection.End(end);
                break;
            default:
                Shapes.End(end);
                break;
        }
    }

    /// <summary>
    /// Ends a running drag without its result – the one way to cancel: the tool or eraser changed, its pointer was
    /// canceled, or its page is replaced or removed (<paramref name="affected"/>: whether the drag's page is concerned).
    /// The pointer is let go; still down, it begins nothing until its next press (gestures begin only with a press). A
    /// taken-over eraser end stays with the app until the tip hovers again; only a newly shown document gives it back at
    /// once (InkWorkspace.Reset).
    /// </summary>
    public void CancelGestureOn(Func<InkPage, bool> affected)
    {
        if (Drag is { IsRunning: true } && DragPage is { } dragPage && affected(dragPage))
        {
            Drag.Cancel();
        }
        if (running is { } r && affected(r.Page))
        {
            Cancel(r);
        }
    }

    private void Cancel(Running r)
    {
        InkLog.Write($"ink input: {r.Tool} canceled");
        Take();
        switch (r.Tool)
        {
            case InkTool.Select:
                Selection.Cancel();
                break;
            case InkTool.Shape:
                Shapes.Cancel();
                break;
            case InkTool.PartialEraser:
                eraser?.End(); // what was erased stays erased (and can be undone)
                break;
        }
    }

    // The running gesture, now ended.
    private Running? Take()
    {
        var taken = running;
        running = null;
        AutoScroll?.Stop();
        return taken;
    }

    // The one test of "down" for a press and a lift: the pen on the surface, the mouse's left button pressed – by the
    // pointer's device (e.Pointer), the same for both.
    private static bool IsDown(Windows.UI.Input.PointerPoint point) =>
        IsPen(point) ? point.IsInContact : point.Properties.IsLeftButtonPressed;

    // The device, always from the pointer (e.Pointer) – one source for every test.
    private static bool IsPen(Windows.UI.Input.PointerPoint point) =>
        point.PointerDevice.PointerDeviceType == Windows.Devices.Input.PointerDeviceType.Pen;

    private static bool IsTouch(Windows.UI.Input.PointerPoint point) =>
        point.PointerDevice.PointerDeviceType == Windows.Devices.Input.PointerDeviceType.Touch;

    // The presenter reports positions in its own (the InkCanvas') coordinates – the coordinates of its strokes and of
    // the page's overlay, which share the InkCanvas' place in the sheet. No transform (sandbox log 2026-11-16).
    // Over another page than the running gesture's (a drag crossing pages), its presenter reports in its own coordinates:
    // converted to the gesture's page (pages differ by a translation).
    private Point LocalPosition(InkPage view, Windows.UI.Input.PointerPoint point) =>
        running is { } r && r.Page != view
            ? view.InkCanvas.TransformToVisual(r.Page.InkCanvas).TransformPoint(point.Position)
            : point.Position;

    // The partial eraser takes the pen's eraser end, and with the eraser tool active also the pen tip and the mouse.
    private bool Erases(bool isPen, bool eraserEnd) =>
        eraser != null && ((isPen && eraserEnd) || EraserToolActive());

    // The pen turned over: inverted while hovering, inverted and/or eraser while touching. The mouse counts as a tip.
    private static bool IsEraserEnd(Windows.UI.Input.PointerPoint point) =>
        IsPen(point) && (point.Properties.IsInverted || point.Properties.IsEraser);
}
