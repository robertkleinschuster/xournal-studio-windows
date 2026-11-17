using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

namespace InkPages;

/// <summary>
/// Moving and scaling the selection with the pen, as XAML pointer input: an app interaction, not ink – Microsoft's
/// samples take the pen from the ink for a non-ink mode, and build only the lasso from the presenter's unprocessed input.
/// While the pen hovers over the selection the presenters leave the pen to XAML (<see cref="ToolController.SelectionTakenOver"/>,
/// set from the hover by <see cref="InkTools"/>); XAML gives the pen's place on any page from screen coordinates, also
/// while the pages scroll. (The presenters' positions drift once the page that has the pen scrolls out of view – measured
/// in the sandbox 2026-11-17 – so a long drag with auto-scroll cannot follow them.)
/// </summary>
public sealed class SelectionDrag(SelectionTool selection, Action<bool> takeOver)
{
    private InkPage? page;        // the drag's page (a hand-over moves it to another page)
    private UIElement? captured;   // the page surface holding the pen
    private uint pointer;

    public DragAutoScroll? AutoScroll { get; set; }

    public bool IsRunning => captured != null;

    public void Attach(InkPage view)
    {
        var surface = view.Surface;
        surface.PointerMoved += (_, e) => Logged("moved", () => OnMoved(view, e));
        surface.PointerPressed += (_, e) => Logged("pressed", () => OnPressed(view, e));
        surface.PointerReleased += (_, e) => Logged("released", () => OnReleased(e));
        surface.PointerCaptureLost += (_, e) => Logged("capture lost", () =>
        {
            if (Owns(e))
            {
                Finish(e.GetCurrentPoint(page!.InkCanvas).Position);
            }
        });
        surface.PointerCanceled += (_, e) => Logged("canceled", () =>
        {
            if (Owns(e))
            {
                Cancel();
            }
        });
        surface.PointerExited += (_, e) => Logged("exited", () =>
        {
            if (!IsRunning && IsPen(e))
            {
                takeOver(false); // off the page (to another one, or away): the ink takes the pen again
            }
        });
    }

    /// <summary>The drag went on to another page (SelectionTool.DragChangedPage): positions are taken there.</summary>
    public void FollowPage(InkPage view)
    {
        if (IsRunning)
        {
            page = view;
            AutoScroll?.FollowPage(view);
        }
    }

    /// <summary>Ends a running drag without its result (tool changed, page removed, pointer canceled).</summary>
    public void Cancel()
    {
        if (captured is not { } c)
        {
            return;
        }
        captured = null;
        AutoScroll?.Stop();
        c.ReleasePointerCaptures();
        selection.Cancel();
        takeOver(false);
    }

    private bool Owns(PointerRoutedEventArgs e) => IsRunning && e.Pointer.PointerId == pointer;

    private static bool IsPen(PointerRoutedEventArgs e) => e.Pointer.PointerDeviceType == PointerDeviceType.Pen;

    private void OnMoved(InkPage view, PointerRoutedEventArgs e)
    {
        if (Owns(e))
        {
            var p = e.GetCurrentPoint(page!.InkCanvas).Position;
            selection.Move(p);
            // The move may have handed the content over to another page (FollowPage): its own coordinates from here.
            AutoScroll?.Track(page, e.GetCurrentPoint(page.InkCanvas).Position, q =>
            {
                if (IsRunning)
                {
                    selection.Move(q);
                }
            });
            e.Handled = true;
        }
        else if (!IsRunning && IsPen(e) && !e.Pointer.IsInContact)
        {
            // Hovering: away from the selection the ink takes the pen again (before it touches).
            takeOver(selection.Grabs(view, e.GetCurrentPoint(view.InkCanvas).Position));
        }
    }

    private void OnPressed(InkPage view, PointerRoutedEventArgs e)
    {
        if (IsRunning || !IsPen(e))
        {
            return;
        }
        var p = e.GetCurrentPoint(view.InkCanvas).Position;
        if (!selection.Grabs(view, p) || !view.Surface.CapturePointer(e.Pointer))
        {
            takeOver(false);
            return;
        }
        captured = view.Surface;
        pointer = e.Pointer.PointerId;
        page = view;
        selection.Begin(view, p, SelectMode.Lasso); // on the selection: it moves (inside) or scales (corner)
        e.Handled = true;
    }

    private void OnReleased(PointerRoutedEventArgs e)
    {
        if (Owns(e))
        {
            Finish(e.GetCurrentPoint(page!.InkCanvas).Position);
            e.Handled = true;
        }
    }


    private void Finish(Point p)
    {
        var c = captured!;
        var view = page!;
        captured = null;
        AutoScroll?.Stop();
        selection.End(p);
        c.ReleasePointerCaptures();
        takeOver(selection.Grabs(view, p)); // still over the selection: the next press moves it again
    }

    // Pointer handlers run from XAML's input dispatch: an exception is logged before it propagates (never swallowed).
    private static void Logged(string name, Action handler)
    {
        try
        {
            handler();
        }
        catch (Exception ex)
        {
            InkLog.Write($"selection drag {name}", ex);
            throw;
        }
    }
}
