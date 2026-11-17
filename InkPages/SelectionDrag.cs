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

    /// <summary>Handles the page surface's pointer input; returns what takes the handlers off again (view released).</summary>
    public Action Attach(InkPage view)
    {
        var surface = view.Surface;
        PointerEventHandler moved = (_, e) => Logged("moved", () => OnMoved(view, e));
        PointerEventHandler pressed = (_, e) => Logged("pressed", () => OnPressed(view, e));
        PointerEventHandler released = (_, e) => Logged("released", () => OnReleased(e));
        PointerEventHandler captureLost = (_, e) => Logged("capture lost", () =>
        {
            if (Owns(e))
            {
                var popups = view.XamlRoot is { } root
                    ? string.Join(", ", Microsoft.UI.Xaml.Media.VisualTreeHelper.GetOpenPopupsForXamlRoot(root)
                        .Select(p => p.Child?.GetType().Name ?? "(empty)"))
                    : "";
                var at = e.GetCurrentPoint(page!.InkCanvas).Position;
                InkLog.Write($"selection drag: pointer capture lost at {at.X:0},{at.Y:0} (open popups: {(popups.Length > 0 ? popups : "none")}, " +
                    $"source {e.OriginalSource?.GetType().Name}, in contact {e.Pointer.IsInContact}, presenter devices " +
                    $"{page.Ink.InputDeviceTypes} mode {page.Ink.InputProcessingConfiguration.Mode}, " +
                    $"focus {(view.XamlRoot is { } r ? Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(r)?.GetType().Name : null)})");
                Finish(e.GetCurrentPoint(page!.InkCanvas).Position);
            }
        });
        PointerEventHandler canceled = (_, e) => Logged("canceled", () =>
        {
            if (Owns(e))
            {
                InkLog.Write("selection drag: pointer canceled");
                Cancel();
            }
        });
        PointerEventHandler exited = (_, e) => Logged("exited", () =>
        {
            if (!IsRunning && IsPen(e))
            {
                takeOver(false); // off the page (to another one, or away): the ink takes the pen again
            }
        });
        surface.PointerMoved += moved;
        surface.PointerPressed += pressed;
        surface.PointerReleased += released;
        surface.PointerCaptureLost += captureLost;
        surface.PointerCanceled += canceled;
        surface.PointerExited += exited;
        return () =>
        {
            surface.PointerMoved -= moved;
            surface.PointerPressed -= pressed;
            surface.PointerReleased -= released;
            surface.PointerCaptureLost -= captureLost;
            surface.PointerCanceled -= canceled;
            surface.PointerExited -= exited;
        };
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
        InkLog.Write("selection drag: canceled");
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
            // Hovering: away from the selection the ink takes the pen again (before it touches); so does the eraser end,
            // which erases also over the selection.
            var point = e.GetCurrentPoint(view.InkCanvas);
            bool eraserEnd = point.Properties.IsInverted || point.Properties.IsEraser;
            takeOver(!eraserEnd && selection.Grabs(view, point.Position));
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
        InkLog.Write($"selection drag: begins (pointer {pointer})");
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
        InkLog.Write("selection drag: ends");
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
