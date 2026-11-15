using Microsoft.UI.Input;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using XournalStudio.Services;
using InkDrawingAttributes = Windows.UI.Input.Inking.InkDrawingAttributes;

namespace XournalStudio.Views;

public enum InkTool
{
    None,
    Select,
    Shape,
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

    public InkTool Tool
    {
        get => tool;
        set
        {
            CancelGesture();
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
        view.Surface.PointerCanceled += (_, _) => CancelGesture();
    }

    private void OnPressed(PageView view, PointerRoutedEventArgs e)
    {
        if (gesture != InkTool.None || e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
        {
            return;
        }
        var point = e.GetCurrentPoint(view.Surface);
        bool isPen = e.Pointer.PointerDeviceType == PointerDeviceType.Pen;
        // The pen's barrel button may arrive as barrel or as right button (Lenovo pens).
        bool barrel = isPen && (point.Properties.IsBarrelButtonPressed || point.Properties.IsRightButtonPressed);
        bool down = isPen ? point.IsInContact : point.Properties.IsLeftButtonPressed;
        var active = barrel ? InkTool.Select : tool;
        if (active == InkTool.None || !down || point.Properties.IsEraser)
        {
            return;
        }
        last = point.Position;
        if (active == InkTool.Select)
        {
            Selection.Begin(view, last);
        }
        else
        {
            Shapes.Begin(view, last);
        }
        gesture = active;
        gesturePage = view;
        view.Surface.CapturePointer(e.Pointer);
        e.Handled = true;
    }

    private void OnMoved(PageView view, PointerRoutedEventArgs e)
    {
        if (gesture == InkTool.None || gesturePage != view)
        {
            return;
        }
        last = e.GetCurrentPoint(view.Surface).Position;
        if (gesture == InkTool.Select)
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
        if (finished == InkTool.Select)
        {
            Selection.End(end);
        }
        else
        {
            Shapes.End(end);
        }
    }

    private void CancelGesture()
    {
        if (gesture == InkTool.Select)
        {
            Selection.Cancel();
        }
        else if (gesture == InkTool.Shape)
        {
            Shapes.Cancel();
        }
        gesture = InkTool.None;
        gesturePage = null;
    }
}
