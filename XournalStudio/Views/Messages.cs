using Microsoft.UI.Xaml.Controls;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>Messages for the user in the window's InfoBar (and the log).</summary>
public sealed class Messages(InfoBar info)
{
    public void Show(InfoBarSeverity severity, string title, string message)
    {
        DebugLog.Write($"{severity}: {title}: {message}");
        info.Severity = severity;
        info.Title = title;
        info.Message = message;
        info.IsOpen = true;
    }

    /// <summary>Logs the exception completely and tells the user what failed.</summary>
    public void Error(string context, string title, Exception e, string? subject = null)
    {
        DebugLog.Write(context, e);
        Show(InfoBarSeverity.Error, title, subject != null ? $"{subject}: {DebugLog.Describe(e)}" : DebugLog.Describe(e));
    }
}
