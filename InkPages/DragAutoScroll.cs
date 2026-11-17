using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace InkPages;

/// <summary>
/// Scrolls the pages while a drag (selection moved, scaled, lasso, rectangle) holds the pen near the edge of the view:
/// the closer to the edge (or beyond it), the faster. It goes on while the pen holds still, so it runs once per frame
/// (<see cref="CompositionTarget.Rendering"/>, XAML's hook for per-frame work): each frame scrolls by speed × frame time
/// and hands the drag the pen's place on its page, so content and view move together and smoothly.
/// </summary>
public sealed class DragAutoScroll(ScrollView scroller)
{
    private const double Band = 48;          // DIPs inside the view's edge where scrolling begins
    private const double MaxSpeed = 1500;    // DIPs per second at the edge and beyond

    private InkPage? page;
    private Point inView;                    // the pen in the ScrollView's coordinates
    private Action<Point>? moveTo;
    private TimeSpan? lastFrame;
    private bool hooked;

    /// <summary>The drag's pen is at <paramref name="local"/> on <paramref name="dragPage"/> (its page points).</summary>
    public void Track(InkPage dragPage, Point local, Action<Point> move)
    {
        page = dragPage;
        moveTo = move;
        inView = dragPage.InkCanvas.TransformToVisual(scroller).TransformPoint(local);
        if (Velocity() == default)
        {
            Unhook();
        }
        else if (!hooked)
        {
            lastFrame = null;
            CompositionTarget.Rendering += OnRendering;
            hooked = true;
        }
    }

    /// <summary>The drag went on to another page: the pen's place is handed over in that page's points.</summary>
    public void FollowPage(InkPage dragPage)
    {
        if (page != null)
        {
            page = dragPage;
        }
    }

    public void Stop()
    {
        Unhook();
        page = null;
        moveTo = null;
    }

    private void Unhook()
    {
        if (hooked)
        {
            CompositionTarget.Rendering -= OnRendering;
            hooked = false;
        }
    }

    private void OnRendering(object? sender, object e)
    {
        var time = ((RenderingEventArgs)e).RenderingTime;
        double dt = lastFrame is { } last ? Math.Clamp((time - last).TotalSeconds, 0, 0.05) : 0;
        lastFrame = time;
        var v = Velocity();
        if (page == null || moveTo == null || v == default)
        {
            Unhook();
            return;
        }
        // The pen holds still in the view and the content moved under it: the drag follows (XAML's transform is current
        // for the frame being rendered – measured in the sandbox, 2026-11-17).
        moveTo(scroller.TransformToVisual(page.InkCanvas).TransformPoint(inView));
        if (dt > 0)
        {
            scroller.ScrollBy(v.X * dt, v.Y * dt, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        }
    }

    private Point Velocity() => new(Speed(inView.X, scroller.ActualWidth), Speed(inView.Y, scroller.ActualHeight));

    private static double Speed(double at, double size)
    {
        if (size <= 2 * Band)
        {
            return 0;
        }
        if (at < Band)
        {
            return -MaxSpeed * Math.Min(1, (Band - at) / Band);
        }
        if (at > size - Band)
        {
            return MaxSpeed * Math.Min(1, (at - (size - Band)) / Band);
        }
        return 0;
    }
}
