using Microsoft.UI.Xaml.Controls;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// At the start: offers the recovery copies of sessions that ended without the user deciding about their changes
/// (crash, power loss, Windows ended the app), newest first – Wiederherstellen, Verwerfen (to the recycle bin) or
/// Später (offered again at the next start).
/// </summary>
public sealed class RecoveryPrompt(Recovery recovery, Dialogs dialogs, Messages messages)
{
    /// <summary>The copy the user wants restored, or null.</summary>
    public async Task<RecoveryEntry?> ChooseAsync()
    {
        List<RecoveryEntry> abandoned;
        try
        {
            abandoned = recovery.FindAbandoned();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            messages.Error("find recovery copies", "Wiederherstellungskopien nicht lesbar", e);
            return null;
        }
        foreach (var entry in abandoned)
        {
            string name = DisplayName(entry.Info);
            var choice = await dialogs.ShowAsync(new ContentDialog
            {
                Title = "Nicht gespeicherte Änderungen wiederherstellen?",
                Content = Dialogs.Text(Describe(name, entry.Info)),
                PrimaryButtonText = "Wiederherstellen",
                SecondaryButtonText = "Verwerfen",
                CloseButtonText = "Später",
                DefaultButton = ContentDialogButton.Primary,
            });
            if (choice == ContentDialogResult.Primary)
            {
                return entry;
            }
            if (choice == ContentDialogResult.Secondary)
            {
                try
                {
                    await recovery.RecycleAsync(entry, BaseName(entry.Info));
                }
                catch (Exception e)
                {
                    messages.Error($"recycle recovery copy {entry.Id}", "Verwerfen fehlgeschlagen", e, entry.CopyPath);
                }
            }
        }
        return null;
    }

    // The document's name without extension (for the file in the recycle bin).
    private static string BaseName(RecoveryInfo info) =>
        XoppDocument.NameFor(info.DocumentPath, info.PdfPath, info.CreatedAt ?? info.SavedAt);

    private static string DisplayName(RecoveryInfo info) =>
        info.DocumentPath != null ? Path.GetFileName(info.DocumentPath) : BaseName(info) + " (nie gespeichert)";

    private static string Describe(string name, RecoveryInfo info)
    {
        string text = $"„{name}“ wurde nicht ordnungsgemäß geschlossen. Die nicht gespeicherten Änderungen sind " +
            $"gesichert (Stand {info.SavedAt:dd.MM.yyyy HH:mm}).";
        if (info.DocumentPath != null && !File.Exists(info.DocumentPath))
        {
            text += $"\n\nDie Datei {info.DocumentPath} gibt es nicht mehr; beim Speichern wird nach dem Ort gefragt.";
        }
        else if (info.DocumentPath != null && File.GetLastWriteTime(info.DocumentPath) > info.SavedAt)
        {
            text += $"\n\nAchtung: Die Datei wurde danach noch geändert ({File.GetLastWriteTime(info.DocumentPath):dd.MM.yyyy HH:mm}). " +
                "Wiederherstellen und Speichern überschreibt diesen Stand.";
        }
        return text;
    }
}
