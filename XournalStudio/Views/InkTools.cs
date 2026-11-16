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
            if (gesture == InkTool.PartialEraser)
            {
                CancelGestureOn(null); // the running erase ends with the eraser it began with
            }
            eraser = value;
        }
    }

    private PartialEraser? eraser;
    private uint gesturePointer;

    /// <summary>True while the toolbar's eraser is the active tool.</summary>
    public Func<bool> EraserToolActive { get; set; } = () => false;

    /// <summary>The pen's tip (or the mouse) is back: the presenters take ink input again.</summary>
    public Action ReleaseEraserEnd { get; set; } = () => { };

    public InkTool Tool
    {
        get => tool;
        set
        {
            CancelGestureOn(null);
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
        view.Surface.PointerCaptureLost += (_, _) => Finish(view, last);
        view.Surface.PointerCanceled += (_, e) =>
        {
            if (e.Pointer.PointerId == gesturePointer)
            {
                CancelGestureOn(null); // only the gesture's own pointer (a palm or finger beside it is not)
            }
        };
        view.Surface.PointerEntered += (_, e) => OnMoved(view, e); // the eraser's circle appears where the pen comes in
        view.Surface.PointerExited += (_, _) => Eraser?.Hide();
    }

    private void OnPressed(PageView view, PointerRoutedEventArgs e)
    {
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
            if (!IsEraserEnd(point))
            {
                ReleaseEraserEnd(); // the tip hovers: it will ink
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
        if (gesturePage != view)
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
        if (gesture == InkTool.None || gesturePage != view)
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
        var finished = gesture;
        gesture = InkTool.None; // before anything else: releasing the capture raises PointerCaptureLost
        gesturePage = null;
        if (finished == InkTool.PartialEraser)
        {
            Eraser!.End();
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
    /// Ends a running drag without its result – the one way to cancel: the tool changed, the pointer was canceled, or its
    /// page is replaced or removed (<paramref name="on"/>: only a drag on that page; null: any). The pointer is let go.
    /// (A taken-over eraser end stays with the app until the tip hovers again, as after any touch – it may still be down.)
    /// </summary>
    public void CancelGestureOn(PageView? on)
    {
        if (gesture == InkTool.None || (on != null && gesturePage != on))
        {
            return;
        }
        var page = gesturePage;
        var cancelled = gesture;
        gesture = InkTool.None; // first: releasing the capture raises CaptureLost, which then finds no gesture
        gesturePage = null;
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

    // The partial eraser takes the pen's eraser end, and with the eraser tool active also the pen tip and the mouse.
    private bool Erases(bool isPen, bool eraserEnd) =>
        Eraser != null && ((isPen && eraserEnd) || EraserToolActive());

    // The pen turned over: inverted while hovering, inverted and/or eraser while touching. The mouse counts as a tip.
    private static bool IsEraserEnd(Microsoft.UI.Input.PointerPoint point) =>
        point.PointerDeviceType == PointerDeviceType.Pen && (point.Properties.IsInverted || point.Properties.IsEraser);
}
