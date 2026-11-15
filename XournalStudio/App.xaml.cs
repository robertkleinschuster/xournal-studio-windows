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
        // First command line argument (if any) is the file to open.
        var cmd = Environment.GetCommandLineArgs();
        string? file = cmd.Length > 1 ? cmd[1] : null;
        window = new MainWindow(file);
        window.Activate();
    }
}
