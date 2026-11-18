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

    /// <summary>
    /// The document's PDF is <paramref name="path"/> (loaded as <paramref name="pdf"/>): pages of another PDF were
    /// appended to it, or a document without PDF uses the inserted PDF from now on.
    /// </summary>
    public void UsePdf(string path, PdfBackground pdf)
    {
        if (!ProjectFiles.SamePath(Document.PdfPath, path))
        {
            Document.MovePdf(path);
        }
        Pdf = pdf;
    }

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

    // Counts changes (SetDirty(true)): a save or an open that ran a while knows whether the document changed meanwhile.
    private int changes;

    // The save running, if any (it never faults: errors are reported by the save itself). Saves run one after another.
    private Task saving = Task.CompletedTask;

    // Counts opens: an open that finishes after a later one began is dropped.
    private int opens;

    public string Title => (IsDirty ? "• " : "") +
        (Document.FilePath != null ? Path.GetFileName(Document.FilePath) : $"{Document.SuggestedName} (nicht gespeichert)");

    // ---------------------------------------------------------------- showing documents

    /// <summary>
    /// Shows another document. The previous one is left for good (the user saved or discarded its changes, see
    /// <see cref="ConfirmLeaveAsync"/>), so its recovery copy goes; the new one gets its own.
    /// </summary>
    public void Show(XoppDocument doc, PdfBackground? pdf)
    {
        opens++; // an open still loading is not shown over this one
        RememberPosition();
        autosave.StartNewDocument();
        Document = doc;
        Pdf = pdf;
        pages.Show(doc, pdf);
        pages.RestorePosition(PositionFor(doc));
        SetDirty(false);
        FilesChanged?.Invoke(this, EventArgs.Empty);
    }

    private string? PositionKey(XoppDocument doc)
    {
        string? path = doc.FilePath ?? doc.PdfPath;
        return path == null ? null : Path.GetFullPath(path).ToUpperInvariant();
    }

    private DocumentPosition? PositionFor(XoppDocument doc) =>
        PositionKey(doc) is { } key && settings.DocumentPositions.TryGetValue(key, out var position) ? position : null;

    private void RememberPosition()
    {
        if (PositionKey(Document) is not { } key || !pages.HasPages)
        {
            return;
        }
        settings.DocumentPositions[key] = new DocumentPosition
        {
            VerticalOffset = pages.VerticalOffset,
            Zoom = pages.ZoomFactor,
        };
        settings.Save();
    }

    public void ShowNew() => Show(XoppDocument.CreateNew(), null);

    /// <summary>Opens a .xopp, or a PDF as a new unsaved document on it (the first save asks where).</summary>
    public async Task OpenAsync(string path)
    {
        int open = ++opens;
        int before = changes;
        try
        {
            XoppDocument doc;
            PdfBackground? pdf;
            if (Path.GetExtension(path).Equals(".pdf", StringComparison.OrdinalIgnoreCase))
            {
                pdf = await PdfBackground.LoadAsync(path);
                doc = XoppDocument.FromPdf(path, pdf.GetPageSizes());
            }
            else
            {
                // Reading and parsing the file takes a while for a long document: not on the UI thread (the pages' strokes
                // become ink only for the pages near the viewport, also off the UI thread: PageSlots).
                doc = await Task.Run(() => XoppDocument.Load(path));
                pdf = await LoadPdfAsync(doc);
            }
            if (!await StillWantedAsync(open, before))
            {
                return;
            }
            Show(doc, pdf);
            settings.LastFile = path;
            settings.Save();
        }
        catch (Exception e)
        {
            messages.Error("open", "Datei konnte nicht geöffnet werden", e, path);
        }
    }

    /// <summary>
    /// A loaded document is shown only if no later open began meanwhile, and – when the open one was changed while
    /// loading (the user went on writing) – after the same question as before leaving it.
    /// </summary>
    private async Task<bool> StillWantedAsync(int open, int changesBefore)
    {
        if (open != opens)
        {
            return false;
        }
        if (changes != changesBefore && !await ConfirmLeaveAsync())
        {
            return false;
        }
        return open == opens; // (the question may have taken a while)
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
            int open = ++opens;
            int before = changes;
            var doc = await Task.Run(() => XoppDocument.LoadCopy(entry.CopyPath, path, info.ReferenceBase));
            doc.CreatedAt = info.CreatedAt;
            var pdf = await LoadPdfAsync(doc);
            if (!await StillWantedAsync(open, before))
            {
                return false; // the copy stays and is offered again at the next start
            }
            Show(doc, pdf);
            SetDirty(true);
            autosave.WriteNowReplacing(entry.Id); // the restored state is this session's copy now
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

    /// <summary>The window closes (the user saved or discarded the changes): a running save ends, the recovery copy goes.</summary>
    public async Task EndAsync()
    {
        await WhenSaved();
        RememberPosition();
        autosave.Discard();
        await autosave.Idle;
    }

    /// <summary>Completes when no save runs (also one started meanwhile); never faults.</summary>
    public async Task WhenSaved()
    {
        while (!saving.IsCompleted)
        {
            await saving;
        }
    }

    /// <summary>The window lost focus (maybe for good: shut down, log off): the recovery copy is brought up to date.</summary>
    public void Deactivated() => autosave.WriteNow();

    /// <summary>Saves to the document's file (Ctrl+S); false if it has none, saving waits for a sync, or it failed.</summary>
    public Task<bool> SaveAsync() => Document.FilePath is { } path ? SaveAsync(path, saveAs: false) : Task.FromResult(false);

    // Ctrl+S writes the document where it is; Save As (saveAs) takes its attachments along (XoppDocument.SaveAsAsync).
    // The document is snapshot at once; the file is written on the thread pool, one save after another.
    private async Task<bool> SaveAsync(string path, bool saveAs)
    {
        await WhenSaved(); // never two writes at once (of "<file>~" or of attachments)
        if (SaveBlocked && saveAs)
        {
            // Not deferred like Ctrl+S: the picked place would have to wait somewhere unseen; the user saves again.
            messages.Show(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Nicht gespeichert: Speichern unter ist möglich, sobald die Synchronisierung fertig ist.");
            return false;
        }
        if (SaveBlocked)
        {
            SaveRequested = true;
            messages.Show(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Gespeichert wird, sobald die Synchronisierung fertig ist.");
            return false;
        }
        var doc = Document;
        int before = changes;
        var save = saveAs ? doc.SaveAsAsync(path) : doc.SaveAsync(path);
        saving = save.ContinueWith(_ => { }, TaskScheduler.Default);
        try
        {
            await save;
            if (doc == Document && changes == before)
            {
                SetDirty(false);
            }
            else
            {
                StateChanged?.Invoke(this, EventArgs.Empty); // written (the title may name the new file); later changes stay unsaved
            }
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
        if (SaveBlocked)
        {
            // Not deferred like Save: the picked place would have to wait somewhere unseen; the user saves again.
            messages.Show(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Speichern unter ist möglich, sobald die Synchronisierung fertig ist.");
            return false;
        }
        string? picked = await PickSavePathAsync();
        return picked != null && await SaveAsync(picked, saveAs: true); // (refused there if a sync began while the picker was open)
    }

    private async Task<string?> PickSavePathAsync()
    {
        string? folder = Path.GetDirectoryName(Document.FilePath ?? Document.PdfPath) ?? projectFolder();
        var picker = new FileSavePicker(window) { SuggestedFileName = Document.SuggestedName };
        if (folder != null && Directory.Exists(folder))
        {
            picker.SuggestedFolder = folder;
        }
        picker.FileTypeChoices.Add("Xournal++-Dokument", [".xopp"]);
        return (await picker.PickSaveFileAsync())?.Path;
    }

    /// <summary>Speichern (Ctrl+S): to the document's file, or where the user picks for a new one.</summary>
    public async Task<bool> SaveOrSaveAsAsync() => Document.FilePath != null ? await SaveAsync() : await SaveAsAsync();

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
        await WhenSaved(); // a save running decides whether anything is left unsaved
        if (!IsDirty)
        {
            return true;
        }
        var choice = await AskSaveAsync(Document.FilePath != null
            ? "Ohne Speichern gehen die Änderungen seit dem letzten Speichern verloren."
            : "Das Dokument wurde noch nie gespeichert; ohne Speichern geht es verloren.");
        // "Nicht speichern": the copy goes once another document is shown (Show) or the window closes (End) – if
        // opening the next one fails, this one stays open with its changes and its copy.
        return choice != SaveChoice.Cancelled;
    }

    /// <summary>
    /// Writes the document to a file the user picks without making that the open file (the sync's "keep mine as a
    /// copy"): the document stays as it is – its file, its changes, its recovery copy.
    /// </summary>
    public async Task<bool> SaveCopyAsync()
    {
        string? path = await PickSavePathAsync();
        if (path == null)
        {
            return false;
        }
        await WhenSaved();
        var save = Document.SaveCopyAsync(path); // as upstream: the copy gets its own attachments
        saving = save.ContinueWith(_ => { }, TaskScheduler.Default);
        try
        {
            await save;
            return true;
        }
        catch (Exception e)
        {
            messages.Error("save copy", "Speichern fehlgeschlagen", e, path);
            return false;
        }
    }

    private void SetDirty(bool value)
    {
        IsDirty = value;
        if (value)
        {
            changes++;
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
