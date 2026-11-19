using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Messages for the user (and the log) in the window's InfoBar, which floats over the top bar beside the menus – never
/// over the pages (the ink would be drawn over it) and without moving them. Success and information close by themselves
/// after a few seconds, warnings a little later; errors stay until closed.
/// </summary>
public sealed class Messages
{
    private static readonly TimeSpan ShortTime = TimeSpan.FromSeconds(5), LongTime = TimeSpan.FromSeconds(12);

    private readonly InfoBar info;
    private readonly DispatcherQueueTimer closeTimer;

    /// <param name="beside">The element the message is placed right of (the menu bar).</param>
    public Messages(InfoBar info, FrameworkElement beside)
    {
        this.info = info;
        closeTimer = info.DispatcherQueue.CreateTimer();
        closeTimer.IsRepeating = false;
        closeTimer.Tick += (_, _) => info.IsOpen = false;
        info.Closed += (_, _) => closeTimer.Stop();
        beside.SizeChanged += (_, _) => info.Margin = new Thickness(beside.ActualWidth + 16, 4, 0, 0);
    }

    public void Show(InfoBarSeverity severity, string title, string message)
    {
        DebugLog.Write($"{severity}: {title}: {message}");
        info.Severity = severity;
        info.Title = title;
        info.Message = message;
        info.IsOpen = true;
        closeTimer.Stop();
        if (severity != InfoBarSeverity.Error)
        {
            closeTimer.Interval = severity == InfoBarSeverity.Warning ? LongTime : ShortTime;
            closeTimer.Start();
        }
    }

    /// <summary>Logs the exception completely and tells the user what failed.</summary>
    public void Error(string context, string title, Exception e, string? subject = null)
    {
        DebugLog.Write(context, e);
        Show(InfoBarSeverity.Error, title, subject != null ? $"{subject}: {DebugLog.Describe(e)}" : DebugLog.Describe(e));
    }
}
