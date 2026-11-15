using Microsoft.UI.Xaml.Controls;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Git sync of the project folder: asks to save changes first, then commits everything, pulls with rebase and pushes
/// (<see cref="GitSync"/>), and reloads the document if the pull changed it.
/// </summary>
public sealed class SyncController(DocumentSession session, Dialogs dialogs, Messages messages)
{
    public bool IsSyncing { get; private set; }

    /// <summary>A sync started or ended.</summary>
    public event EventHandler? StateChanged;

    public async void Sync(string? root)
    {
        if (IsSyncing || root == null)
        {
            return;
        }
        if (session.IsDirty && await session.AskSaveAsync("Hochgeladen wird nur, was gespeichert ist. Ohne Speichern " +
            "bleiben die Änderungen ungespeichert im Fenster.") == SaveChoice.Cancelled)
        {
            return;
        }
        SetSyncing(true);
        try
        {
            var result = await Task.Run(() =>
                GitSync.Sync(root, GitCredentials.Get, Environment.MachineName, DateTimeOffset.Now));
            var doc = session.Document;
            bool documentChanged = result.PulledFiles.Any(f => SamePath(f, doc.FilePath) || SamePath(f, doc.PdfPath));
            if (!documentChanged || await ResolvePulledDocumentAsync())
            {
                messages.Show(InfoBarSeverity.Success, "Synchronisiert", Describe(result));
            }
        }
        catch (GitSyncConflictException ex)
        {
            DebugLog.Write("sync conflict", ex);
            messages.Show(InfoBarSeverity.Error, "Konflikt beim Synchronisieren",
                $"{ex.Message}. Die eigenen Änderungen sind committet, aber nicht hochgeladen; den Konflikt mit Git lösen.");
        }
        catch (Exception ex)
        {
            messages.Error("sync", "Synchronisieren fehlgeschlagen", ex);
        }
        finally
        {
            SetSyncing(false);
            // Saving was asked for during the sync (Ctrl+S) and the pull did not change the document.
            if (session.SaveRequested && session.IsDirty && session.Document.FilePath != null)
            {
                session.Save();
            }
            session.SaveRequested = false;
            StateChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    private void SetSyncing(bool value)
    {
        IsSyncing = value;
        session.SaveBlocked = value;
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private static bool SamePath(string a, string? b) =>
        b != null && string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// The pull changed (or removed) the open document. With unsaved notes in it the user decides; otherwise it is
    /// reloaded – from the PDF when the .xopp is gone, or closed when nothing is left. False: a message is shown.
    /// </summary>
    private async Task<bool> ResolvePulledDocumentAsync()
    {
        session.SaveRequested = false; // asked for before the document changed on disk: the user decides again
        if (session.IsDirty)
        {
            var choice = await dialogs.ShowAsync(new ContentDialog
            {
                Title = "Dokument wurde auf einem anderen Gerät geändert",
                Content = Dialogs.Text("Die Synchronisierung hat das offene Dokument geändert, während es neue, nicht " +
                    "gespeicherte Notizen hatte."),
                PrimaryButtonText = "Neu laden (meine Notizen verwerfen)",
                SecondaryButtonText = "Meine als Kopie speichern…",
                CloseButtonText = "Meine behalten",
                DefaultButton = ContentDialogButton.Secondary,
            });
            if (choice == ContentDialogResult.None)
            {
                KeepOwn("Die geholte Version wird beim Speichern mit der eigenen überschrieben; sie bleibt in der Git-Historie.");
                return false;
            }
            if (choice == ContentDialogResult.Secondary)
            {
                bool saved = await session.SaveCopyAsync(); // git's work is done by now
                if (!saved)
                {
                    KeepOwn("Keine Kopie gespeichert: die geholte Version wird beim Speichern mit der eigenen " +
                        "überschrieben; sie bleibt in der Git-Historie.");
                    return false;
                }
            }
        }

        var doc = session.Document;
        if (doc.FilePath != null && File.Exists(doc.FilePath))
        {
            await session.OpenAsync(doc.FilePath);
            return true;
        }
        if (doc.PdfPath != null && File.Exists(doc.PdfPath))
        {
            await session.OpenAsync(doc.PdfPath);
            return true;
        }
        string? path = doc.FilePath;
        session.ShowNew();
        messages.Show(InfoBarSeverity.Warning, "Dokument wurde auf einem anderen Gerät gelöscht oder umbenannt",
            $"{path} gibt es nach der Synchronisierung nicht mehr.");
        return false;
    }

    // The user keeps the own version: it stays open with its changes (saved only when the user saves).
    private void KeepOwn(string text) =>
        messages.Show(InfoBarSeverity.Warning, "Eigene Version behalten", text);

    private static string Describe(GitSyncResult result)
    {
        var parts = new List<string>();
        if (result.PulledFiles.Count > 0)
        {
            parts.Add(result.PulledFiles.Count == 1 ? "1 Datei geholt" : $"{result.PulledFiles.Count} Dateien geholt");
        }
        if (result.Pushed)
        {
            parts.Add(result.Committed ? "Änderungen hochgeladen" : "Commits hochgeladen");
        }
        return parts.Count > 0 ? string.Join(", ", parts) + "." : "Alles war schon aktuell.";
    }
}
