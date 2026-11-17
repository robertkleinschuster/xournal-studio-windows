using Microsoft.UI.Xaml;

namespace InkPages.Demo;

/// <summary>
/// The demo app. Command line: <c>--dark</c> or <c>--light</c> sets the theme (default: the system's). The environment
/// variable INKPAGES_DEMO_LOG names a log file (the library's <see cref="InkLog"/> and unhandled exceptions).
/// </summary>
public partial class App : Application
{
    private DemoWindow? window;

    public App()
    {
        InitializeComponent();
        if (Environment.GetEnvironmentVariable("INKPAGES_DEMO_LOG") is { Length: > 0 } log)
        {
            InkLog.Writer = message => File.AppendAllText(log, $"{DateTime.Now:HH:mm:ss.fff} {message}{Environment.NewLine}");
        }
        // Exceptions must never vanish silently.
        UnhandledException += (_, e) => InkLog.Write("unhandled", e.Exception);
        TaskScheduler.UnobservedTaskException += (_, e) => InkLog.Write("unobserved task", e.Exception);
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        var options = Environment.GetCommandLineArgs().Skip(1).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var theme = options.Contains("--dark") ? ElementTheme.Dark : options.Contains("--light") ? ElementTheme.Light : ElementTheme.Default;
        window = new DemoWindow(theme);
        window.Activate();
    }
}
