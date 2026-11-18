namespace XournalStudio.Services;

/// <summary>
/// The app's log. Everything goes to the file named by XOURNAL_STUDIO_LOG, if set (for tests and diagnosis). Errors –
/// exceptions, unhandled ones included – also go to <c>errors.log</c> in the app's folder, always: a crash on the device
/// must leave its exception behind (exceptions must never vanish). That file is started anew once it exceeds 1 MB (the
/// previous one is kept as <c>errors.old.log</c>).
/// </summary>
public static class DebugLog
{
    private const long MaxErrorLog = 1024 * 1024;
    private static readonly string? FilePath = Environment.GetEnvironmentVariable("XOURNAL_STUDIO_LOG");
    private static readonly object ErrorLock = new();

    public static void Write(string message)
    {
        if (FilePath != null)
        {
            File.AppendAllText(FilePath, Line(message));
        }
    }

    /// <summary>Logs an exception completely (type, HResult, message, stack) – exceptions must never vanish.</summary>
    public static void Write(string context, Exception e) =>
        Error($"{context}: {e.GetType().FullName} 0x{e.HResult:X8} {e}");

    /// <summary>An error: to the log and always to <c>errors.log</c> in the app's folder.</summary>
    public static void Error(string message)
    {
        Write(message);
        string line = $"{DateTime.Now:yyyy-MM-dd} {Line(message)}";
        lock (ErrorLock)
        {
            try
            {
                Directory.CreateDirectory(AppSettings.Folder);
                string path = Path.Combine(AppSettings.Folder, "errors.log");
                var file = new FileInfo(path);
                if (file.Exists && file.Length > MaxErrorLog)
                {
                    File.Move(path, Path.Combine(AppSettings.Folder, "errors.old.log"), overwrite: true);
                }
                File.AppendAllText(path, line);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                // The error log itself cannot be written (disk full, locked): the message goes to the test log, if any.
                Write($"errors.log not written: {e.GetType().FullName} 0x{e.HResult:X8} {e.Message}");
            }
        }
    }

    /// <summary>A text for the user: the message, or type and code when the message is empty (COM errors).</summary>
    public static string Describe(Exception e) =>
        string.IsNullOrWhiteSpace(e.Message) ? $"{e.GetType().Name} 0x{e.HResult:X8}" : e.Message;

    private static string Line(string message) => $"{DateTime.Now:HH:mm:ss.fff} {message}{Environment.NewLine}";
}
