using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace XournalStudio;

public partial class App : Application
{
    private MainWindow? window;

    // The app's code begins (the module is loaded before Main runs).
    [System.Runtime.CompilerServices.ModuleInitializer]
    internal static void ModuleLoaded() => Startup("app module loaded");

    public App()
    {
        Startup("app constructor");
        InitializeComponent();
        Startup("app XAML loaded");
        // Exceptions must never vanish silently (a swallowed one once led to a wrong diagnosis).
        // Logged always (errors.log), so a crash on the device leaves its exception behind; unhandled stays unhandled.
        UnhandledException += (_, e) => Services.DebugLog.Error(
            $"Unhandled: {e.Exception?.GetType().FullName} 0x{e.Exception?.HResult:X8} {e.Message} {e.Exception}");
        TaskScheduler.UnobservedTaskException += (_, e) => Services.DebugLog.Error($"Unobserved task: {e.Exception}");
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Services.DebugLog.Error($"Unhandled (thread): {e.ExceptionObject}");
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Startup("launched");
        // Windows' Application Restart: after a crash or a restart for updates Windows starts the app again, which then
        // offers the recovery copy of unsaved changes (see Services.Recovery). No arguments: it reopens the last file.
        int result = RegisterApplicationRestart("", 0);
        if (result != 0)
        {
            Services.DebugLog.Write($"RegisterApplicationRestart failed: 0x{result:X8}");
        }

        // First command line argument (if any) is the file to open.
        var cmd = Environment.GetCommandLineArgs();
        string? file = cmd.Length > 1 ? cmd[1] : null;
        window = new MainWindow(file);
        Startup("main window built");
        if (window.Content is FrameworkElement content)
        {
            content.Loaded += (_, _) => Startup("content loaded");
        }
        window.Activate();
        Startup("activated");
    }

    // How long after the process started (for measuring the start; the log's later lines carry their own time).
    private static void Startup(string step) => Services.DebugLog.Write(
        $"start: {step} {(DateTime.Now - System.Diagnostics.Process.GetCurrentProcess().StartTime).TotalMilliseconds:F0} ms after the process started");

    // https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-registerapplicationrestart (S_OK = 0)
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int RegisterApplicationRestart(string? commandLine, int flags);
}
