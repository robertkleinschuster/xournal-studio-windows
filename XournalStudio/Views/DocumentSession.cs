using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

public enum SaveChoice { Saved, NotSaved, Cancelled }

/// <summary>
/// The open document: opening, saving only when the user chooses it, the same Speichern / Nicht speichern / Abbrechen
/// question whenever it is left, and the recovery copy of unsaved changes (<see cref="Autosave"/>).
/// </summary>
public sealed class DocumentSession
{
    private readonly WindowId window;
    private readonly PagesController pages;
    private readonly Dialogs dialogs;
    private readonly Messages messages;
    private readonly AppSettings settings;
    private readonly Func<string?> projectFolder;
    private readonly Recovery recovery = Recovery.ForApp();
    private readonly Autosave autosave;
    private readonly DateTime processStart = Recovery.CurrentProcessStart();

    public DocumentSession(WindowId window, PagesController pages, Dialogs dialogs, Messages messages,
        AppSettings settings, Func<string?> projectFolder)
    {
        this.window = window;
        this.pages = pages;
        this.dialogs = dialogs;
        this.messages = messages;
        this.settings = settings;
        this.projectFolder = projectFolder;
        autosave = new Autosave(recovery, Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread(), Snapshot);
        autosave.Failed += (_, e) => messages.Show(InfoBarSeverity.Warning, "Wiederherstellungskopie fehlgeschlagen",
            $"Nicht gespeicherte Änderungen sind bei einem Absturz nicht gesichert: {DebugLog.Describe(e)}");
    }

    public XoppDocument Document { get; private set; } = XoppDocument.CreateNew();

    public PdfBackground? Pdf { get; private set; }

    public bool IsDirty { get; private set; }

    /// <summary>The file the explorer marks as open: the document, or the PDF of one never saved.</summary>
    public string? OpenPath => Document.FilePath ?? Document.PdfPath;

    /// <summary>While set (git sync running), saving waits; a save asked for meanwhile is <see cref="SaveRequested"/>.</summary>
    public bool SaveBlocked { get; set; }

    public bool SaveRequested { get; set; }

    /// <summary>Title or dirty state changed.</summary>
    public event EventHandler? StateChanged;

    /// <summary>Another document is shown, or the document was saved (files on disk changed).</summary>
    public event EventHandler? FilesChanged;

    public string Title => (IsDirty ? "• " : "") +
        (Document.FilePath != null ? Path.GetFileName(Document.FilePath) : $"{Document.SuggestedName} (nicht gespeichert)");

    // ---------------------------------------------------------------- showing documents

    /// <summary>
    /// Shows another document. The previous one is left for good (the user saved or discarded its changes, see
    /// <see cref="ConfirmLeaveAsync"/>), so its recovery copy goes; the new one gets its own.
    /// </summary>
    public void Show(XoppDocument doc, PdfBackground? pdf)
    {
        autosave.StartNewDocument();
        Document = doc;
        Pdf = pdf;
        pages.Show(doc, pdf);
        SetDirty(false);
        FilesChanged?.Invoke(this, EventArgs.Empty);
    }

    public void ShowNew() => Show(XoppDocument.CreateNew(), null);

    /// <summary>Opens a .xopp, or a PDF as a new unsaved document on it (the first save asks where).</summary>
    public async Task OpenAsync(string path)
    {
        try
        {
            if (Path.GetExtension(path).Equals(".pdf", StringComparison.OrdinalIgnoreCase))
            {
                var background = await PdfBackground.LoadAsync(path);
                Show(XoppDocument.FromPdf(path, background.GetPageSizes()), background);
            }
            else
            {
                var doc = XoppDocument.Load(path);
                Show(doc, await LoadPdfAsync(doc));
            }
            settings.LastFile = path;
            settings.Save();
        }
        catch (Exception e)
        {
            messages.Error("open", "Datei konnte nicht geöffnet werden", e, path);
        }
    }

    /// <summary>Offers recovery copies of crashed sessions; true if one was restored (it is open, unsaved).</summary>
    public async Task<bool> OfferRecoveryAsync()
    {
        var entry = await new RecoveryPrompt(recovery, dialogs, messages).ChooseAsync();
        if (entry == null)
        {
            return false;
        }
        try
        {
            var info = entry.Info;
            string? path = info.DocumentPath != null && File.Exists(info.DocumentPath) ? info.DocumentPath : null;
            var doc = XoppDocument.LoadCopy(entry.CopyPath, path, info.ReferenceBase);
            doc.CreatedAt = info.CreatedAt;
            Show(doc, await LoadPdfAsync(doc));
            SetDirty(true);
            autosave.WriteNow();          // the restored state is this session's copy now ...
            autosave.Delete(entry.Id);    // ... and the old one goes once that is written
            return true;
        }
        catch (Exception e)
        {
            messages.Error($"restore {entry.Id}", "Wiederherstellung fehlgeschlagen", e, $"die Kopie bleibt erhalten: {entry.CopyPath}");
            return false;
        }
    }

    private async Task<PdfBackground?> LoadPdfAsync(XoppDocument doc)
    {
        if (doc.PdfPath == null)
        {
            return null;
        }
        try
        {
            return await PdfBackground.LoadAsync(doc.PdfPath);
        }
        catch (Exception e)
        {
            DebugLog.Write("load PDF background", e);
            messages.Show(InfoBarSeverity.Warning, "PDF-Hintergrund nicht gefunden", $"{doc.PdfPath}: {DebugLog.Describe(e)}");
            return null;
        }
    }

    // ---------------------------------------------------------------- changes and saving

    /// <summary>The document was changed.</summary>
    public void MarkChanged() => SetDirty(true);

    /// <summary>The background PDF was renamed: the document refers to the new name (written on the next save).</summary>
    public void MovePdf(string newPath)
    {
        Document.MovePdf(newPath);
        SetDirty(true);
    }

    /// <summary>The window lost focus (maybe for good: shut down, log off): the recovery copy is brought up to date.</summary>
    public void Deactivated() => autosave.WriteNow();

    /// <summary>Completes when the recovery copy is written or deleted (before the process ends).</summary>
    public Task IdleAsync() => autosave.Idle;

    /// <summary>Saves to the document's file (Ctrl+S); false if it has none, saving waits for a sync, or it failed.</summary>
    public bool Save()
    {
        string? path = Document.FilePath;
        if (path == null)
        {
            return false;
        }
        if (SaveBlocked)
        {
            SaveRequested = true;
            messages.Show(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Gespeichert wird, sobald die Synchronisierung fertig ist.");
            return false;
        }
        try
        {
            Document.Save(path);
            SetDirty(false);
            settings.LastFile = path;
            settings.Save();
            FilesChanged?.Invoke(this, EventArgs.Empty);
            return true;
        }
        catch (Exception e)
        {
            messages.Error("save", "Speichern fehlgeschlagen", e, path);
            return false;
        }
    }

    /// <summary>Saves where the user picks: suggested is the document's name in its folder, the PDF's or the project's.</summary>
    public async Task<bool> SaveAsAsync()
    {
        string? folder = Path.GetDirectoryName(Document.FilePath ?? Document.PdfPath) ?? projectFolder();
        var picker = new FileSavePicker(window) { SuggestedFileName = Document.SuggestedName };
        if (folder != null && Directory.Exists(folder))
        {
            picker.SuggestedFolder = folder;
        }
        picker.FileTypeChoices.Add("Xournal++-Dokument", [".xopp"]);
        var result = await picker.PickSaveFileAsync();
        if (result == null)
        {
            return false;
        }
        string? previous = Document.FilePath;
        Document.FilePath = result.Path;
        if (!Save())
        {
            Document.FilePath = previous;
            return false;
        }
        return true;
    }

    /// <summary>Speichern (Ctrl+S): to the document's file, or where the user picks for a new one.</summary>
    public async Task<bool> SaveOrSaveAsAsync() => Document.FilePath != null ? Save() : await SaveAsAsync();

    /// <summary>Asks whether to save the changes. Nothing is saved without the user choosing it.</summary>
    public async Task<SaveChoice> AskSaveAsync(string reason)
    {
        string name = Document.FilePath != null ? Path.GetFileName(Document.FilePath) : Document.SuggestedName;
        var choice = await dialogs.ShowAsync(new ContentDialog
        {
            Title = $"Änderungen an „{name}“ speichern?",
            Content = Dialogs.Text(reason),
            PrimaryButtonText = "Speichern",
            SecondaryButtonText = "Nicht speichern",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        });
        return choice switch
        {
            ContentDialogResult.Primary => await SaveOrSaveAsAsync() ? SaveChoice.Saved : SaveChoice.Cancelled,
            ContentDialogResult.Secondary => SaveChoice.NotSaved,
            _ => SaveChoice.Cancelled,
        };
    }

    /// <summary>
    /// Before the document is left (another one opened, the window closed, the file renamed or deleted) – always the
    /// same question. True: go on (saved or discarded as the user chose); false: stay.
    /// </summary>
    public async Task<bool> ConfirmLeaveAsync()
    {
        if (!IsDirty)
        {
            return true;
        }
        var choice = await AskSaveAsync(Document.FilePath != null
            ? "Ohne Speichern gehen die Änderungen seit dem letzten Speichern verloren."
            : "Das Dokument wurde noch nie gespeichert; ohne Speichern geht es verloren.");
        if (choice == SaveChoice.NotSaved)
        {
            autosave.Discard(); // discarded by choice
        }
        return choice != SaveChoice.Cancelled;
    }

    /// <summary>Saves under a new name without making that the open file (the sync's "keep mine as a copy").</summary>
    public async Task<bool> SaveCopyAsync()
    {
        string? original = Document.FilePath;
        bool saved = await SaveAsAsync();
        Document.FilePath = original;
        return saved;
    }

    private void SetDirty(bool value)
    {
        IsDirty = value;
        if (value)
        {
            autosave.Changed();
        }
        else
        {
            autosave.Discard(); // saved, or a document without changes shown: nothing to recover
        }
        StateChanged?.Invoke(this, EventArgs.Empty);
    }

    private (System.Xml.Linq.XDocument, RecoveryInfo) Snapshot()
    {
        var info = new RecoveryInfo(Document.FilePath, Document.ReferenceBase, Document.PdfPath, Document.CreatedAt,
            DateTime.Now, Environment.ProcessId, processStart);
        return (Document.Snapshot(info.ReferenceBase), info);
    }
}
