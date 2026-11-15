namespace XournalStudio.Services;

/// <summary>Appends to the file named by XOURNAL_STUDIO_LOG, if set (for tests).</summary>
public static class DebugLog
{
    private static readonly string? FilePath = Environment.GetEnvironmentVariable("XOURNAL_STUDIO_LOG");

    public static void Write(string message)
    {
        if (FilePath != null)
        {
            File.AppendAllText(FilePath, $"{DateTime.Now:HH:mm:ss.fff} {message}{Environment.NewLine}");
        }
    }

    /// <summary>Logs an exception completely (type, HResult, message, stack) – exceptions must never vanish.</summary>
    public static void Write(string context, Exception e) =>
        Write($"{context}: {e.GetType().FullName} 0x{e.HResult:X8} {e}");

    /// <summary>A text for the user: the message, or type and code when the message is empty (COM errors).</summary>
    public static string Describe(Exception e) =>
        string.IsNullOrWhiteSpace(e.Message) ? $"{e.GetType().Name} 0x{e.HResult:X8}" : e.Message;
}
