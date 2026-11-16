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
    private InkTool tool;
    private InkTool gesture; // the tool of the drag in progress
    private PageView? gesturePage;
    private Point last;

    public SelectionTool Selection { get; } = new(undo);

    public ShapeTool Shapes { get; } = new(undo, pen);

    /// <summary>Set while erasing is partial (see <see cref="ToolController.PartialErasing"/>).</summary>
    public PartialEraser? Eraser
    {
        get => eraser;
        set
        {
            if (value != eraser && gesture == InkTool.PartialEraser)
            {
                CancelGestureOn(_ => true); // the running erase ends with the eraser it began with
            }
            eraser = value;
        }
    }

    private PartialEraser? eraser;
    private uint? gesturePointer; // the running gesture's pointer (null: none)
    private uint? cancelledPointer; // its gesture was cancelled while it was down: nothing new until it is lifted

    /// <summary>True while the toolbar's eraser is the active tool.</summary>
    public Func<bool> EraserToolActive { get; set; } = () => false;

    /// <summary>The pen's tip (or the mouse) is back: the presenters take ink input again.</summary>
    public Action ReleaseEraserEnd { get; set; } = () => { };

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
        // A running gesture belongs to its pointer: another one (a palm, a finger beside the pen) neither moves, finishes
        // nor cancels it.
        view.Surface.PointerCaptureLost += (_, e) =>
        {
            if (e.Pointer.PointerId == gesturePointer)
            {
                Finish(view, last);
            }
        };
        view.Surface.PointerCanceled += (_, e) =>
        {
            if (e.Pointer.PointerId == gesturePointer)
            {
                CancelGestureOn(_ => true);
            }
        };
        view.Surface.PointerEntered += (_, e) => OnMoved(view, e); // the eraser's circle appears where the pen comes in
        view.Surface.PointerExited += (_, e) =>
        {
            Eraser?.Hide();
            if (!e.GetCurrentPoint(view.Surface).IsInContact)
            {
                Lifted(e.Pointer.PointerId);
            }
        };
    }

    // The pointer is seen not down (a release, a move or exit without contact, a new press – not a capture loss or a
    // cancel, which may come with the pen still down): a gesture cancelled under it may begin anew.
    private void Lifted(uint pointer)
    {
        if (pointer == cancelledPointer)
        {
            cancelledPointer = null;
        }
    }

    // A cancelled pointer begins nothing (and shows no eraser circle) until it is lifted – the one check for every start.
    private bool MayBegin(PointerRoutedEventArgs e) => e.Pointer.PointerId != cancelledPointer;

    private void OnPressed(PageView view, PointerRoutedEventArgs e)
    {
        Lifted(e.Pointer.PointerId); // a press is a new contact
        if (gesture != InkTool.None || e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
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
            Eraser!.Begin(view, last);
        }
        else if (active == InkTool.Select)
        {
            Selection.Begin(view, last, barrel ? SelectMode.Lasso : Selection.Mode); // barrel: always the lasso (user)
        }
        else
        {
            Shapes.Begin(view, last);
        }
        gesture = active;
        gesturePage = view;
        gesturePointer = e.Pointer.PointerId;
        view.Surface.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void OnMoved(PageView view, PointerRoutedEventArgs e)
    {
        if (gesture == InkTool.None)
        {
            var point = e.GetCurrentPoint(view.Surface);
            bool isPen = e.Pointer.PointerDeviceType == PointerDeviceType.Pen;
            if (e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
            {
                return;
            }
            if (!point.IsInContact)
            {
                Lifted(e.Pointer.PointerId);
            }
            if (!IsEraserEnd(point))
            {
                ReleaseEraserEnd(); // the tip hovers: it will ink
            }
            if (!MayBegin(e))
            {
                Eraser?.Hide();
                return;
            }
            if (isPen && point.IsInContact && IsEraserEnd(point) && Eraser != null)
            {
                // The eraser end was taken over during its touch (see PagesController): erasing goes on from here.
                last = point.Position;
                Eraser.Begin(view, last);
                gesture = InkTool.PartialEraser;
                gesturePage = view;
                gesturePointer = e.Pointer.PointerId;
                view.Surface.CapturePointer(e.Pointer);
                e.Handled = true;
                return;
            }
            // Hovering: the partial eraser shows its circle.
            if (Erases(isPen, IsEraserEnd(point)))
            {
                Eraser!.Hover(view, point.Position);
            }
            else
            {
                Eraser?.Hide();
            }
            return;
        }
        if (gesturePage != view || e.Pointer.PointerId != gesturePointer)
        {
            return;
        }
        last = e.GetCurrentPoint(view.Surface).Position;
        if (gesture == InkTool.PartialEraser)
        {
            Eraser!.Move(last);
        }
        else if (gesture == InkTool.Select)
        {
            Selection.Move(last);
        }
        else
        {
            Shapes.Move(last);
        }
        e.Handled = true;
    }

    private void OnReleased(PageView view, PointerRoutedEventArgs e)
    {
        Lifted(e.Pointer.PointerId);
        if (gesture == InkTool.None || gesturePage != view || e.Pointer.PointerId != gesturePointer)
        {
            return;
        }
        Finish(view, e.GetCurrentPoint(view.Surface).Position);
        view.Surface.ReleasePointerCapture(e.Pointer);
        e.Handled = true;
    }

    /// <summary>Completes the drag at <paramref name="end"/> (released, or the pointer capture was lost).</summary>
    private void Finish(PageView view, Point end)
    {
        if (gesture == InkTool.None || gesturePage != view)
        {
            return;
        }
        var (finished, _) = TakeGesture();
        if (finished == InkTool.PartialEraser)
        {
            eraser!.End();
        }
        else if (finished == InkTool.Select)
        {
            Selection.End(end);
        }
        else
        {
            Shapes.End(end);
        }
    }

    /// <summary>
    /// Ends a running drag without its result – the one way to cancel: the tool or eraser changed, its pointer was
    /// canceled, or its page is replaced or removed (<paramref name="affected"/>: whether the drag's page is concerned).
    /// The pointer is let go and begins nothing new until it is seen lifted (a release, a move or exit without contact,
    /// a new press). (A taken-over eraser end stays with the app until the tip hovers again – it may still be
    /// down; only a newly shown document gives it back at once, see PagesController.Show.)
    /// </summary>
    public void CancelGestureOn(Func<PageView, bool> affected)
    {
        if (gesture == InkTool.None || gesturePage is not { } running || !affected(running))
        {
            return;
        }
        cancelledPointer = gesturePointer;
        var (cancelled, page) = TakeGesture();
        if (cancelled == InkTool.Select)
        {
            Selection.Cancel();
        }
        else if (cancelled == InkTool.Shape)
        {
            Shapes.Cancel();
        }
        else if (cancelled == InkTool.PartialEraser)
        {
            eraser?.End(); // what was erased stays erased (and can be undone)
        }
        page?.Surface.ReleasePointerCaptures();
    }

    // The running gesture and its page, now ended – first of all: releasing a capture raises CaptureLost, which then
    // finds no gesture (and no pointer).
    private (InkTool Gesture, PageView? Page) TakeGesture()
    {
        var taken = (gesture, gesturePage);
        gesture = InkTool.None;
        gesturePage = null;
        gesturePointer = null;
        return taken;
    }

    // The partial eraser takes the pen's eraser end, and with the eraser tool active also the pen tip and the mouse.
    private bool Erases(bool isPen, bool eraserEnd) =>
        Eraser != null && ((isPen && eraserEnd) || EraserToolActive());

    // The pen turned over: inverted while hovering, inverted and/or eraser while touching. The mouse counts as a tip.
    private static bool IsEraserEnd(Microsoft.UI.Input.PointerPoint point) =>
        point.PointerDeviceType == PointerDeviceType.Pen && (point.Properties.IsInverted || point.Properties.IsEraser);
}
