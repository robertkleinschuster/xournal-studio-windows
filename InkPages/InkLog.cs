namespace InkPages;

/// <summary>
/// Where the library logs (exceptions are logged before they propagate, never swallowed): the app sets
/// <see cref="Writer"/> to its own log; by default it goes to the debugger's output.
/// </summary>
public static class InkLog
{
    public static Action<string> Writer { get; set; } = message => System.Diagnostics.Debug.WriteLine(message);

    public static void Write(string message) => Writer(message);

    /// <summary>An exception completely: type, HResult, message, stack.</summary>
    public static void Write(string context, Exception e) =>
        Writer($"{context}: {e.GetType().FullName} 0x{e.HResult:X8} {e}");
}
