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
        foreach (var entry in recovery.FindAbandoned())
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
                    await recovery.RecycleAsync(entry, name);
                }
                catch (Exception e)
                {
                    messages.Error($"recycle recovery copy {entry.Id}", "Verwerfen fehlgeschlagen", e, entry.CopyPath);
                }
            }
        }
        return null;
    }

    private static string DisplayName(RecoveryInfo info) =>
        info.DocumentPath != null ? Path.GetFileName(info.DocumentPath)
        : (info.PdfPath != null ? Path.GetFileNameWithoutExtension(info.PdfPath)
            : XoppDocument.DefaultName(info.CreatedAt ?? info.SavedAt)) + " (nie gespeichert)";

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
