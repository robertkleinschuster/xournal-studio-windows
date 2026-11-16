using Microsoft.UI.Input;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;

namespace XournalStudio.Views;

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
/// The pen's barrel button selects with every tool: the presenters ignore barrel input
/// (<c>IsPrimaryBarrelButtonInputEnabled = false</c>, see <see cref="ToolController"/>), so it arrives here as well.
/// </summary>
public sealed class InkTools(UndoStack undo, Func<InkDrawingAttributes> pen)
{
    // The drag in progress: its tool, its page and its pointer – a gesture belongs to its pointer, another one (a palm,
    // a finger beside the pen) neither moves, finishes nor cancels it.
    private sealed record Running(InkTool Tool, PageView Page, uint Pointer);

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

    /// <summary>
    /// The eraser end's touch belongs to the app (ToolController.EraserEndTakenOver – the one owner of that state, set
    /// when Windows ink's erase was put back, given back when the tip hovers): its moves on a page erase partially.
    /// </summary>
    public Func<bool> EraserEndTakenOver { get; set; } = () => false;

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

    /// <summary>Takes pointer input of a page (once per page view).</summary>
    public void Attach(PageView view)
    {
        view.Surface.PointerPressed += (_, e) => OnPressed(view, e);
        view.Surface.PointerMoved += (_, e) => OnMoved(view, e);
        view.Surface.PointerReleased += (_, e) => OnReleased(view, e);
        view.Surface.PointerCaptureLost += (_, e) =>
        {
            if (Owns(view, e))
            {
                Finish(last);
            }
        };
        view.Surface.PointerCanceled += (_, e) =>
        {
            if (Owns(view, e))
            {
                Cancel(running!);
            }
        };
        view.Surface.PointerEntered += (_, e) => OnMoved(view, e); // the eraser's circle appears where the pen comes in
        view.Surface.PointerExited += (_, e) =>
        {
            eraser?.Hide();
            EndIfLifted(view, e);
        };
    }

    // The running gesture is on this page and of this pointer.
    private bool Owns(PageView view, PointerRoutedEventArgs e) =>
        running is { } r && r.Page == view && r.Pointer == e.Pointer.PointerId;

    // The running gesture's pointer is seen lifted anywhere (a hover or exit without contact, a release on another page
    // – when the capture did not keep it on the gesture's page): the gesture ends where it was last.
    private bool EndIfLifted(PageView view, PointerRoutedEventArgs e)
    {
        if (running is not { } r || r.Pointer != e.Pointer.PointerId || e.GetCurrentPoint(view.Surface).IsInContact)
        {
            return false;
        }
        Finish(last);
        r.Page.Surface.ReleasePointerCaptures();
        return true;
    }

    private void OnPressed(PageView view, PointerRoutedEventArgs e)
    {
        if (running != null || e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
        {
            return;
        }
        var point = e.GetCurrentPoint(view.Surface);
        bool isPen = e.Pointer.PointerDeviceType == PointerDeviceType.Pen;
        if (!IsEraserEnd(point))
        {
            ReleaseEraserEnd();
        }
        // The pen's barrel button may arrive as barrel or as right button (Lenovo pens).
        bool barrel = isPen && (point.Properties.IsBarrelButtonPressed || point.Properties.IsRightButtonPressed);
        bool down = isPen ? point.IsInContact : point.Properties.IsLeftButtonPressed;
        var active = barrel ? InkTool.Select : tool;
        if (!barrel && Erases(isPen, IsEraserEnd(point))) // the barrel always lassos
        {
            active = InkTool.PartialEraser;
        }
        else if (IsEraserEnd(point))
        {
            return; // the eraser end erases natively
        }
        if (active == InkTool.None || !down)
        {
            return;
        }
        last = point.Position;
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
        Start(active, view, e);
    }

    private void OnMoved(PageView view, PointerRoutedEventArgs e)
    {
        if (running == null)
        {
            OnHover(view, e);
            return;
        }
        if (EndIfLifted(view, e) || !Owns(view, e))
        {
            return; // (a hovering pen never moves, lassos or erases)
        }
        last = e.GetCurrentPoint(view.Surface).Position;
        switch (running.Tool)
        {
            case InkTool.PartialEraser:
                eraser!.Move(last);
                break;
            case InkTool.Select:
                Selection.Move(last);
                break;
            default:
                Shapes.Move(last);
                break;
        }
        e.Handled = true;
    }

    // No gesture: the eraser end taken over during its touch erases on from here; hovering shows the eraser's circle.
    private void OnHover(PageView view, PointerRoutedEventArgs e)
    {
        if (e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
        {
            return;
        }
        var point = e.GetCurrentPoint(view.Surface);
        bool isPen = e.Pointer.PointerDeviceType == PointerDeviceType.Pen;
        if (!IsEraserEnd(point))
        {
            ReleaseEraserEnd(); // the tip hovers: it will ink
        }
        if (EraserEndTakenOver() && isPen && point.IsInContact && IsEraserEnd(point) && eraser != null)
        {
            last = point.Position;
            eraser.Begin(view, last);
            Start(InkTool.PartialEraser, view, e);
            return;
        }
        if (!point.IsInContact && Erases(isPen, IsEraserEnd(point))) // the circle while hovering
        {
            eraser!.Hover(view, point.Position);
        }
        else
        {
            eraser?.Hide();
        }
    }

    private void Start(InkTool active, PageView view, PointerRoutedEventArgs e)
    {
        running = new Running(active, view, e.Pointer.PointerId);
        if (!view.Surface.CapturePointer(e.Pointer))
        {
            // Without the capture its release elsewhere is not seen here; leaving the page lifted ends it (PointerExited).
            DebugLog.Write($"{active}: the page did not capture the pointer");
        }
        e.Handled = true;
    }

    private void OnReleased(PageView view, PointerRoutedEventArgs e)
    {
        if (!Owns(view, e))
        {
            EndIfLifted(view, e); // released over another page
            return;
        }
        Finish(e.GetCurrentPoint(view.Surface).Position);
        view.Surface.ReleasePointerCapture(e.Pointer);
        e.Handled = true;
    }

    /// <summary>Completes the drag at <paramref name="end"/> (released, or the pointer capture was lost).</summary>
    private void Finish(Point end)
    {
        if (Take() is not { } finished)
        {
            return;
        }
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
    /// The pointer is let go. If it is still down, what it does next counts (an eraser end still touching erases on where
    /// it is – undoable). A taken-over eraser end stays with the app until the tip hovers again; only a newly shown
    /// document gives it back at once (PagesController.Show).
    /// </summary>
    public void CancelGestureOn(Func<PageView, bool> affected)
    {
        if (running is { } r && affected(r.Page))
        {
            Cancel(r);
        }
    }

    private void Cancel(Running r)
    {
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
        r.Page.Surface.ReleasePointerCaptures();
    }

    // The running gesture, now ended – first of all: releasing a capture raises CaptureLost, which then finds none.
    private Running? Take()
    {
        var taken = running;
        running = null;
        return taken;
    }

    // The partial eraser takes the pen's eraser end, and with the eraser tool active also the pen tip and the mouse.
    private bool Erases(bool isPen, bool eraserEnd) =>
        eraser != null && ((isPen && eraserEnd) || EraserToolActive());

    // The pen turned over: inverted while hovering, inverted and/or eraser while touching. The mouse counts as a tip.
    private static bool IsEraserEnd(Microsoft.UI.Input.PointerPoint point) =>
        point.PointerDeviceType == PointerDeviceType.Pen && (point.Properties.IsInverted || point.Properties.IsEraser);
}
