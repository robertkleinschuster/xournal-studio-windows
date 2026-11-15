using System.Text.Json;

namespace XournalStudio.Services;

/// <summary>Small JSON settings file in %LOCALAPPDATA%\XournalStudioForWindows (the app is unpackaged).</summary>
public sealed class AppSettings
{
    public string? LastFile { get; set; }

    /// <summary>Folder shown in the explorer (a git repository or any folder).</summary>
    public string? ProjectFolder { get; set; }

    public bool ExplorerOpen { get; set; } = true;

    public bool RecognizeShapes { get; set; }

    public Model.ShapeKind Shape { get; set; }

    private static string LocalAppData => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

    private static string? TestFolder => Environment.GetEnvironmentVariable("XOURNAL_STUDIO_CONFIG_DIR");

    // XOURNAL_STUDIO_CONFIG_DIR overrides the folder (for tests).
    private static string FilePath =>
        Path.Combine(TestFolder ?? Path.Combine(LocalAppData, "XournalStudioForWindows"), "settings.json");

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
