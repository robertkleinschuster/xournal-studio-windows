using System.Text.Json;

namespace XournalStudio.Services;

/// <summary>Small JSON settings file in %LOCALAPPDATA%\XournalStudioForWindows (the app is unpackaged).</summary>
public sealed class AppSettings
{
    public string? LastFile { get; set; }

    /// <summary>Last viewport for each opened document path.</summary>
    public Dictionary<string, DocumentPosition> DocumentPositions { get; set; } = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Folder shown in the explorer (a git repository or any folder).</summary>
    public string? ProjectFolder { get; set; }

    public bool ExplorerOpen { get; set; } = true;

    /// <summary>The side pane shows the tasks (todo.txt) instead of the files.</summary>
    public bool TasksShown { get; set; }

    public bool DoneTasksShown { get; set; }

    public bool RecognizeShapes { get; set; }

    public double PenStrokeWidth { get; set; } = 1.41;

    /// <summary>The toolbar's pen presets (color index in the palette, width); empty: the defaults.</summary>
    public List<PenSetting> Pens { get; set; } = [];

    /// <summary>The toolbar's highlighter presets; empty: the defaults (yellow, green at <see cref="HighlighterStrokeWidth"/>).</summary>
    public List<PenSetting> Highlighters { get; set; } = [];

    public double HighlighterStrokeWidth { get; set; } = 8.5;

    public InkPages.ShapeKind Shape { get; set; }

    public InkPages.SelectMode SelectMode { get; set; }

    /// <summary>
    /// The eraser erases whole strokes (the toolbar's mode toggle); otherwise exactly a circle of <see cref="EraserRadius"/>
    /// points (the default since 2026-11-18; the former setting PartialErasing, off by default, is no longer read).
    /// </summary>
    public bool WholeStrokeErasing { get; set; }

    public double EraserRadius { get; set; } = 4;

    private static string LocalAppData => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

    private static string? TestFolder => Environment.GetEnvironmentVariable("XOURNAL_STUDIO_CONFIG_DIR");

    /// <summary>The app's own folder (settings, recovery); XOURNAL_STUDIO_CONFIG_DIR overrides it (for tests).</summary>
    public static string Folder => TestFolder ?? Path.Combine(LocalAppData, "XournalStudioForWindows");

    private static string FilePath => Path.Combine(Folder, "settings.json");

    // Settings of the app's earlier name.
    private static string OldFilePath => Path.Combine(LocalAppData, "XournalStudioWinUI", "settings.json");

    public static AppSettings Load()
    {
        try
        {
            string path = File.Exists(FilePath) || TestFolder != null ? FilePath : OldFilePath;
            return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(path)) ?? new AppSettings();
        }
        catch (Exception e)
        {
            DebugLog.Write("load settings (defaults used)", e);
            return new AppSettings();
        }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this));
        }
        catch (Exception e)
        {
            // Settings are a convenience only.
            DebugLog.Write("save settings", e);
        }
    }
}

public sealed class PenSetting
{
    public int ColorIndex { get; set; }
    public double Width { get; set; }
}

public sealed class DocumentPosition
{
    public double VerticalOffset { get; set; }
    public float Zoom { get; set; }
}
