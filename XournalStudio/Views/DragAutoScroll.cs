using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace XournalStudio.Views;

/// <summary>
/// Scrolls the pages while a drag (selection moved, scaled, lasso, rectangle) holds the pen near the edge of the view:
/// the closer to the edge (or beyond it), the faster. It goes on while the pen holds still, so a timer drives it; each
/// step hands the drag the pen's new place on its page.
/// </summary>
public sealed class DragAutoScroll
{
    private const double Band = 48;          // DIPs inside the view's edge where scrolling begins
    private const double MaxSpeed = 1500;    // DIPs per second at the edge and beyond

    private readonly ScrollView scroller;
    private readonly DispatcherQueueTimer timer;
    private PageView? page;
    private Point inView;                    // the pen in the ScrollView's coordinates (for the speed)
    private Point anchor;                    // the pen on its page when it last moved (page points)
    private Point anchorOffset;              // the scroll offsets then
    private Action<Point>? moveTo;
    private DateTime lastTick;

    public DragAutoScroll(ScrollView scroller)
    {
        this.scroller = scroller;
        timer = scroller.DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(16);
        timer.Tick += (_, _) => Step();
    }

    /// <summary>The drag's pen is at <paramref name="local"/> on <paramref name="dragPage"/> (its page points).</summary>
    public void Track(PageView dragPage, Point local, Action<Point> move)
    {
        page = dragPage;
        moveTo = move;
        inView = dragPage.InkCanvas.TransformToVisual(scroller).TransformPoint(local);
        Rebase(local);
        if (Velocity() == default)
        {
            timer.Stop();
        }
        else if (!timer.IsRunning)
        {
            lastTick = DateTime.UtcNow;
            timer.Start();
        }
    }

    /// <summary>The drag went on to another page: the pen's place is now given in that page's points.</summary>
    public void FollowPage(PageView dragPage, Point local)
    {
        if (page != null)
        {
            page = dragPage;
            Rebase(local);
        }
    }

    private void Rebase(Point local)
    {
        anchor = local;
        anchorOffset = new Point(scroller.HorizontalOffset, scroller.VerticalOffset);
    }

    public void Stop()
    {
        timer.Stop();
        page = null;
        moveTo = null;
    }

    private void Step()
    {
        var now = DateTime.UtcNow;
        double dt = Math.Min(0.05, (now - lastTick).TotalSeconds);
        lastTick = now;
        var v = Velocity();
        if (page == null || moveTo == null || v == default)
        {
            timer.Stop();
            return;
        }
        // The pen stayed where it is in the view, the content moved under it by what has been scrolled since the pen
        // last moved: the drag follows by exactly that (the ScrollView's offsets are what is shown; a XAML transform
        // lags behind them while the view scrolls, and the dragged content drifted away from the pen).
        double zoom = scroller.ZoomFactor;
        moveTo(new Point(anchor.X + (scroller.HorizontalOffset - anchorOffset.X) / zoom,
            anchor.Y + (scroller.VerticalOffset - anchorOffset.Y) / zoom));
        scroller.ScrollBy(v.X * dt, v.Y * dt, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
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
