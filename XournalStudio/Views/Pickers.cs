using Microsoft.UI;
using Microsoft.Windows.Storage.Pickers;

namespace XournalStudio.Views;

/// <summary>The Windows App SDK pickers as the app uses them: one place for the file types and their names.</summary>
public static class Pickers
{
    public const string Document = ".xopp";
    public const string Pdf = ".pdf";

    /// <summary>A file of one of these types (<see cref="Document"/>, <see cref="Pdf"/>); null: canceled.</summary>
    public static async Task<string?> OpenAsync(WindowId window, params string[] types)
    {
        var picker = new FileOpenPicker(window);
        foreach (string type in types)
        {
            picker.FileTypeFilter.Add(type);
        }
        return (await picker.PickSingleFileAsync())?.Path;
    }

    /// <summary>Where to write a file of <paramref name="type"/>, suggested by name and folder; null: canceled.</summary>
    public static async Task<string?> SaveAsync(WindowId window, string type, string suggestedName, string? folder)
    {
        var picker = new FileSavePicker(window) { SuggestedFileName = suggestedName };
        if (folder != null && Directory.Exists(folder))
        {
            picker.SuggestedFolder = folder;
        }
        picker.FileTypeChoices.Add(type == Pdf ? "PDF-Dokument" : "Xournal++-Dokument", [type]);
        return (await picker.PickSaveFileAsync())?.Path;
    }

    /// <summary>A folder; null: canceled.</summary>
    public static async Task<string?> FolderAsync(WindowId window) =>
        (await new FolderPicker(window).PickSingleFolderAsync())?.Path;
}
