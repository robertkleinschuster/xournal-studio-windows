using Microsoft.UI.Xaml.Controls;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Carries out what the explorer asks for – opening a file, new document/folder, rename, delete – taking care of the
/// open document when it is affected.
/// </summary>
public sealed class ExplorerCommands(
    ExplorerView explorer, DocumentSession session, Dialogs dialogs, Messages messages,
    Action focusDocument, Action refreshExplorer, Action<int> scrollToPage)
{
    /// <summary>.xopp and .pdf open here; everything else in the app Windows has for it, as in File Explorer.</summary>
    public async void OnFileInvoked(object? sender, string path)
    {
        string ext = Path.GetExtension(path);
        if (!ext.Equals(".xopp", StringComparison.OrdinalIgnoreCase) && !ext.Equals(".pdf", StringComparison.OrdinalIgnoreCase))
        {
            try
            {
                var file = await Windows.Storage.StorageFile.GetFileFromPathAsync(Path.GetFullPath(path));
                if (!await Windows.System.Launcher.LaunchFileAsync(file))
                {
                    messages.Show(InfoBarSeverity.Warning, "Datei konnte nicht geöffnet werden", path);
                }
            }
            catch (Exception e)
            {
                messages.Error($"launch {path}", "Datei konnte nicht geöffnet werden", e, path);
            }
            return;
        }
        await OpenDocumentAsync(path, -1);
    }

    /// <summary>
    /// Opens a .xopp or PDF (from the explorer or a task's link): the open one is left with the save question, nothing
    /// opens during a sync; then the page (0-based, -1: the start) is scrolled to. The document takes the keyboard (as
    /// an app opened from File Explorer does): otherwise Del meant for a selection would delete the file in the tree.
    /// </summary>
    public async Task OpenDocumentAsync(string path, int pageIndex)
    {
        if (!File.Exists(path))
        {
            messages.Show(InfoBarSeverity.Warning, "Dokument nicht gefunden", path);
            return;
        }
        if (!await session.LeaveAndOpenAsync(path))
        {
            return; // not opened (refused during a sync, canceled, or the message says why)
        }
        if (pageIndex >= 0)
        {
            scrollToPage(pageIndex);
        }
        focusDocument();
    }

    /// <summary>New document/folder, rename, delete from the explorer's context menu or keys (F2, Del).</summary>
    public async void OnCommand(object? sender, ExplorerCommandRequest request)
    {
        if (session.RefusedWhileSyncing("Das Ändern von Dateien und Ordnern"))
        {
            return;
        }
        string parent = request.IsFolder ? request.Path : Path.GetDirectoryName(request.Path)!;
        try
        {
            switch (request.Command)
            {
                case ExplorerCommand.NewDocument:
                    await NewDocumentAsync(parent);
                    break;
                case ExplorerCommand.NewFolder:
                    await NewFolderAsync(parent);
                    break;
                case ExplorerCommand.Rename:
                    await RenameAsync(request.Path, request.IsFolder);
                    break;
                case ExplorerCommand.Delete:
                    await DeleteAsync(request.Path, request.IsFolder);
                    break;
                case ExplorerCommand.Discard:
                    await DiscardAsync(request.Path);
                    break;
            }
        }
        catch (Exception e)
        {
            string title = request.Command switch
            {
                ExplorerCommand.NewDocument => "Dokument konnte nicht erstellt werden",
                ExplorerCommand.NewFolder => "Ordner konnte nicht erstellt werden",
                ExplorerCommand.Rename => "Umbenennen fehlgeschlagen",
                ExplorerCommand.Discard => "Änderungen konnten nicht verworfen werden",
                _ => "Löschen fehlgeschlagen",
            };
            messages.Error($"explorer {request.Command} {request.Path}", title, e, request.Path);
        }
        refreshExplorer();
    }

    /// <summary>A new, empty document saved in <paramref name="folder"/> (named like Xournal++: date and time) and opened.</summary>
    private async Task NewDocumentAsync(string folder)
    {
        string initial = Path.GetFileNameWithoutExtension(
            ProjectFiles.UniqueName(folder, XoppDocument.DefaultName(DateTime.Now), ProjectFiles.DocumentExtension));
        string? name = await dialogs.AskNameAsync("Neues Dokument", initial, "Erstellen",
            n => ValidateNewName(folder, n + ProjectFiles.DocumentExtension, null));
        if (name == null || !await session.ConfirmLeaveAsync())
        {
            return;
        }
        string path = Path.Combine(folder, name + ProjectFiles.DocumentExtension);
        await XoppDocument.CreateNew().SaveAsync(path);
        await session.OpenAsync(path);
    }

    private async Task NewFolderAsync(string folder)
    {
        string? name = await dialogs.AskNameAsync("Neuer Ordner", ProjectFiles.UniqueName(folder, "Neuer Ordner"), "Erstellen",
            n => ValidateNewName(folder, n, null));
        if (name != null)
        {
            explorer.SelectAfterRefresh(await ProjectFiles.CreateFolderAsync(folder, name));
        }
    }

    /// <summary>
    /// Renames exactly this file (only the name before the extension is edited) or folder – nothing else is renamed
    /// with it. The open document, if it is renamed (or lies in the folder), is left (save question) and opened again
    /// from its new place; if its background PDF is renamed, the document refers to the new name.
    /// </summary>
    private async Task RenameAsync(string path, bool isFolder)
    {
        string parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(path))!;
        string? edited = await dialogs.AskNameAsync("Umbenennen", ProjectFiles.EditableName(path, isFolder), "Umbenennen",
            n => ValidateNewName(parent, ProjectFiles.NewName(path, isFolder, n), path));
        if (edited == null)
        {
            return;
        }
        string newName = ProjectFiles.NewName(path, isFolder, edited);
        if (newName == Path.GetFileName(Path.TrimEndingDirectorySeparator(path)))
        {
            return;
        }
        string? openFile = session.Document.FilePath;
        bool fileAffected = ProjectFiles.IsAtOrBelow(openFile, path);
        if (fileAffected && !await session.ConfirmLeaveAsync())
        {
            return;
        }
        string renamed = await ProjectFiles.RenameAsync(path, isFolder, newName);
        explorer.SelectAfterRefresh(renamed);
        if (fileAffected)
        {
            await session.OpenAsync(ProjectFiles.MapRenamed(openFile!, path, renamed));
        }
        else if (ProjectFiles.IsAtOrBelow(session.Document.PdfPath, path))
        {
            session.MovePdf(ProjectFiles.MapRenamed(session.Document.PdfPath!, path, renamed));
        }
    }

    /// <summary>
    /// Moves exactly this file or folder to the recycle bin after asking. If the open document (or its PDF) is
    /// deleted, it is left first (save question).
    /// </summary>
    private async Task DeleteAsync(string path, bool isFolder)
    {
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        if (!await dialogs.ConfirmAsync($"„{name}“ löschen?", isFolder
                ? "Der Ordner wird mit allem, was darin ist, in den Papierkorb verschoben."
                : "Die Datei wird in den Papierkorb verschoben.", "Löschen"))
        {
            return;
        }
        if (session.Document.Uses(path)) // the document, its PDF or a picture background of it (or a folder holding one)
        {
            if (!await session.ConfirmLeaveAsync())
            {
                return;
            }
            session.ShowNew();
        }
        await ProjectFiles.DeleteAsync(path, isFolder);
    }

    /// <summary>
    /// Gives exactly this file its committed content back (git), after asking. If the open document shows it (the
    /// document itself, its PDF or one of its attachments <c>&lt;doc&gt;.xopp.*</c>), the question says that the
    /// document's unsaved changes go too – no save question (saving would only be overwritten) – and the document is
    /// opened again from the restored files.
    /// </summary>
    private async Task DiscardAsync(string path)
    {
        string? root = GitSync.FindRoot(path);
        if (root == null)
        {
            return; // only modified files offer it, and those lie in a repository
        }
        await session.WhenSaved(); // a save running must not write after the checkout
        string? open = session.OpenPath;
        var doc = session.Document;
        bool affected = doc.Uses(path); // the document, its PDF or a picture background it shows
        string name = Path.GetFileName(path);
        if (!await dialogs.ConfirmAsync($"Änderungen an „{name}“ verwerfen?",
                "Die Datei bekommt wieder den Stand des letzten Commits (der letzten Synchronisierung). Was seitdem an ihr " +
                "geändert wurde, geht verloren." + (affected && session.IsDirty
                    ? " Auch die ungespeicherten Änderungen im offenen Dokument gehen verloren."
                    : ""), "Verwerfen"))
        {
            return;
        }
        if (affected)
        {
            // Left before the checkout (its unsaved changes go, as asked): nothing can save it over the restored file
            // meanwhile, and if opening it again fails, the old state is not left open to be saved later.
            await session.WhenSaved();
            session.ShowNew();
        }
        await Task.Run(() => GitSync.Discard(root, path));
        if (affected && open != null)
        {
            await session.OpenAsync(open); // shows the restored state (a failure is reported by OpenAsync)
        }
    }

    private static string? ValidateNewName(string folder, string name, string? current)
    {
        string? error = ProjectFiles.ValidateName(name);
        string path = Path.Combine(folder, name);
        if (error == null && Path.Exists(path) && !string.Equals(path, current, StringComparison.OrdinalIgnoreCase))
        {
            error = $"In diesem Ordner gibt es „{name}“ schon.";
        }
        return error;
    }
}
