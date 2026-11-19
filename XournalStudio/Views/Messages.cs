using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Messages for the user (and the log) as a toast at the bottom of the window, over the pages: the InfoBar lies in a
/// windowed popup (drawn above the ink; in-tree XAML would be drawn under it), so the pages never move. Success and
/// information close by themselves after a few seconds; warnings and errors stay until closed (they tell of something
/// the user may have to act on – a kept own version, a missing PDF – and must not pass unseen while writing).
/// </summary>
public sealed class Messages
{
    private static readonly TimeSpan ShowTime = TimeSpan.FromSeconds(5);
    private const double Gap = 24; // above the window's bottom edge

    private readonly Popup toast;
    private readonly InfoBar info;
    private readonly FrameworkElement area;
    private readonly DispatcherQueueTimer closeTimer;

    /// <param name="area">The window's content: the toast lies centered at its bottom.</param>
    public Messages(Popup toast, InfoBar info, FrameworkElement area)
    {
        this.toast = toast;
        this.info = info;
        this.area = area;
        closeTimer = info.DispatcherQueue.CreateTimer();
        closeTimer.IsRepeating = false;
        closeTimer.Tick += (_, _) => info.IsOpen = false;
        info.Closed += (_, _) =>
        {
            closeTimer.Stop();
            toast.IsOpen = false;
        };
        area.SizeChanged += (_, _) => Place();
        ((FrameworkElement)toast.Child).SizeChanged += (_, _) => Place();
    }

    public void Show(InfoBarSeverity severity, string title, string message)
    {
        DebugLog.Write($"{severity}: {title}: {message}");
        info.Severity = severity;
        info.Title = title;
        info.Message = message;
        info.IsOpen = true;
        toast.XamlRoot ??= area.XamlRoot;
        toast.IsOpen = true;
        Place();
        closeTimer.Stop();
        if (severity is InfoBarSeverity.Success or InfoBarSeverity.Informational)
        {
            closeTimer.Interval = ShowTime;
            closeTimer.Start();
        }
    }

    /// <summary>Logs the exception completely and tells the user what failed.</summary>
    public void Error(string context, string title, Exception e, string? subject = null)
    {
        DebugLog.Write(context, e);
        Show(InfoBarSeverity.Error, title, subject != null ? $"{subject}: {DebugLog.Describe(e)}" : DebugLog.Describe(e));
    }

    // Centered at the bottom of the window (popup offsets are relative to the window's content).
    private void Place()
    {
        var card = (FrameworkElement)toast.Child;
        card.Measure(new Windows.Foundation.Size(Math.Min(720, Math.Max(0, area.ActualWidth - 2 * Gap)), double.PositiveInfinity));
        var size = card.DesiredSize;
        toast.HorizontalOffset = Math.Max(Gap, (area.ActualWidth - size.Width) / 2);
        toast.VerticalOffset = Math.Max(0, area.ActualHeight - size.Height - Gap);
    }
}
