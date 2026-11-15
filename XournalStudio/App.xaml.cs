using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace XournalStudio;

public partial class App : Application
{
    private MainWindow? window;

    public App()
    {
        InitializeComponent();
        // Exceptions must never vanish silently (a swallowed one once led to a wrong diagnosis).
        UnhandledException += (_, e) => Services.DebugLog.Write($"Unhandled: {e.Exception}");
        TaskScheduler.UnobservedTaskException += (_, e) => Services.DebugLog.Write($"Unobserved task: {e.Exception}");
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
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
        window.Activate();
    }

    // https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-registerapplicationrestart (S_OK = 0)
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int RegisterApplicationRestart(string? commandLine, int flags);
}
