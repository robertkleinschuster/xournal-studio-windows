using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Model;
using XournalStudio.Services;
using XournalStudio.Views;

namespace XournalStudio;

/// <summary>
/// The window: wires menus, toolbar and explorer to the feature classes (<see cref="DocumentSession"/>,
/// <see cref="PagesController"/>, <see cref="SyncController"/>, <see cref="ExplorerCommands"/>, the library's <see cref="InkWorkspace"/>)
/// and holds no feature logic of its own.
/// </summary>
public sealed partial class MainWindow : Window
{
    private readonly AppSettings settings = AppSettings.Load();
    private readonly InkWorkspace ink;
    private readonly Messages messages;
    private readonly Dialogs dialogs;
    private readonly PagesController pages;
    private readonly DocumentSession session;
    private readonly SyncController sync;
    private readonly EditCommands edit;
    private readonly PdfExportCommand export;
    private readonly PenToolbar bar;
    private bool closeConfirmed;

    public MainWindow(string? fileToOpen)
    {
        InitializeComponent();
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"));
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.Maximize();
        }

        messages = new Messages(Info);
        InkLog.Writer = DebugLog.Write;
        bar = AppToolbar.Create(settings);
        ToolbarHost.Content = bar;
        AddAppButtons();
        SetToolChoices();
        ink = new InkWorkspace(bar, Scroller, settings.PartialErasing);
        KeepToolSettings();
        pages = new PagesController(Scroller, PagesPanel, ink);
        dialogs = new Dialogs(() => Content.XamlRoot, () => pages.Views);
        session = new DocumentSession(AppWindow.Id, pages, dialogs, messages, settings, () => ProjectFolder);
        sync = new SyncController(session, dialogs, messages);
        var explorerCommands = new ExplorerCommands(Explorer, session, sync, dialogs, messages, FocusDocument, RefreshExplorer, index =>
        {
            Root.UpdateLayout(); // a document just opened: its pages are laid out before scrolling
            pages.ScrollToPage(index);
        });
        edit = new EditCommands(ink.Selection, pages, ink.Undo, messages, () => bar.Ink.ActiveTool = bar.Select);
        ink.TransferToPage = edit.TransferToPage;
        export = new PdfExportCommand(AppWindow.Id, session, messages);
        ink.Undo.Changed += (_, _) =>
        {
            UndoMenuItem.IsEnabled = ink.Undo.CanUndo;
            RedoMenuItem.IsEnabled = ink.Undo.CanRedo;
            session.MarkChanged();
        };
        pages.PagesChanged += (_, _) => session.MarkChanged();
        session.StateChanged += (_, _) => Title = $"{session.Title} – Xournal Studio";
        session.FilesChanged += (_, _) => RefreshExplorer();
        sync.StateChanged += (_, _) =>
        {
            SyncMenuItem.IsEnabled = Explorer.RepositoryRoot != null && !sync.IsSyncing;
            Explorer.IsSyncing = sync.IsSyncing;
            if (!sync.IsSyncing)
            {
                RefreshExplorer();
            }
        };

        Explorer.FileInvoked += explorerCommands.OnFileInvoked;
        Explorer.CommandRequested += explorerCommands.OnCommand;
        Explorer.SyncRequested += (_, _) => sync.Sync(Explorer.RepositoryRoot);
        Explorer.FolderChanged += (_, _) => RefreshExplorer();
        Explorer.Failed += (_, m) => messages.Show(InfoBarSeverity.Warning, m.Title, m.Message);
        SetExplorerOpen(settings.ExplorerOpen);

        Tasks.Initialize(dialogs, () => (session.Document.FilePath, pages.CurrentIndex), () => sync.IsSyncing);
        Tasks.ShowsDone = settings.DoneTasksShown;
        Tasks.ShowsDoneChanged += (_, _) =>
        {
            settings.DoneTasksShown = Tasks.ShowsDone;
            settings.Save();
        };
        Tasks.Failed += (_, m) => messages.Show(InfoBarSeverity.Warning, m.Title, m.Message);
        Tasks.OpenRequested += async (_, link) => await explorerCommands.OpenDocumentAsync(link.Document, link.PageIndex);
        (settings.TasksShown ? TasksTab : FilesTab).IsSelected = true;

        Activated += OnActivated;
        AppWindow.Closing += OnClosing;

        session.ShowNew();
        Root.Loaded += async (_, _) => await StartAsync(fileToOpen);
    }

    /// <summary>Offers what a crashed session left behind first, then opens the file of the command line or the last one.</summary>
    private async Task StartAsync(string? fileToOpen)
    {
        if (await session.OfferRecoveryAsync())
        {
            if (fileToOpen != null)
            {
                messages.Show(InfoBarSeverity.Informational, "Wiederhergestelltes Dokument geöffnet",
                    $"{Path.GetFileName(fileToOpen)} wurde nicht geöffnet; nach dem Speichern über Datei → Öffnen.");
            }
            return;
        }
        string? file = fileToOpen ?? settings.LastFile;
        if (file != null && File.Exists(file))
        {
            await session.OpenAsync(file);
        }
    }

    private void FocusDocument()
    {
        if (!Scroller.Focus(FocusState.Programmatic))
        {
            DebugLog.Write("focus document: not focusable");
        }
    }

    // ---------------------------------------------------------------- window

    private void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        if (args.WindowActivationState == WindowActivationState.Deactivated)
        {
            session.Deactivated();
        }
        else if (!sync.IsSyncing)
        {
            RefreshExplorer(); // files may have changed outside the app (refreshed after a sync anyway)
        }
    }

    private async void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (closeConfirmed)
        {
            return;
        }
        args.Cancel = true;
        if (!await session.ConfirmLeaveAsync())
        {
            return;
        }
        closeConfirmed = true;
        await session.EndAsync(); // the recovery copy is gone before the process ends
        await Services.PdfRenderer.Shared.ShutdownAsync(); // its GPU device goes while the app still runs (see there)
        Close();
    }

    // ---------------------------------------------------------------- file

    private async void OnNew(object sender, RoutedEventArgs e)
    {
        if (await session.ConfirmLeaveAsync())
        {
            session.ShowNew();
        }
    }

    private async void OnOpen(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker(AppWindow.Id);
        picker.FileTypeFilter.Add(".xopp");
        picker.FileTypeFilter.Add(".pdf");
        var result = await picker.PickSingleFileAsync();
        if (result != null && await session.ConfirmLeaveAsync())
        {
            await session.OpenAsync(result.Path);
        }
    }

    private async void OnSave(object sender, RoutedEventArgs e) => await session.SaveOrSaveAsAsync();

    private async void OnSaveAs(object sender, RoutedEventArgs e) => await session.SaveAsAsync();

    private void OnSync(object sender, RoutedEventArgs e) => sync.Sync(Explorer.RepositoryRoot);

    private async void OnExportPdf(object sender, RoutedEventArgs e) => await export.ExportAsync();

    // ---------------------------------------------------------------- edit

    private void OnUndo(object sender, RoutedEventArgs e)
    {
        if (edit.RefusedWhileDragging("Rückgängig"))
        {
            return;
        }
        ink.UndoStep();
    }

    private void OnRedo(object sender, RoutedEventArgs e)
    {
        if (edit.RefusedWhileDragging("Wiederholen"))
        {
            return;
        }
        ink.RedoStep();
    }

    private async void OnCut(object sender, RoutedEventArgs e) => await edit.CutAsync();

    private async void OnCopy(object sender, RoutedEventArgs e) => await edit.CopyAsync();

    private async void OnPaste(object sender, RoutedEventArgs e) => await edit.PasteAsync();

    private void OnDuplicate(object sender, RoutedEventArgs e) => edit.Duplicate();

    private void OnDeleteSelection(object sender, RoutedEventArgs e) => edit.Delete();

    // The selection's buttons in the command row take no focus; a click on one is work on the page, so the keyboard
    // focus goes to the document (out of the tasks box, where the accelerators would not fire). The menu and its
    // accelerators leave the focus where it is. (Not tab stops: from the keyboard the same commands are the
    // accelerators and the Bearbeiten menu.)
    private void OnRowCut(object sender, RoutedEventArgs e) { FocusDocument(); OnCut(sender, e); }

    private void OnRowCopy(object sender, RoutedEventArgs e) { FocusDocument(); OnCopy(sender, e); }

    private void OnRowDuplicate(object sender, RoutedEventArgs e) { FocusDocument(); OnDuplicate(sender, e); }

    private void OnRowDelete(object sender, RoutedEventArgs e) { FocusDocument(); OnDeleteSelection(sender, e); }

    private void OnSelectAll(object sender, RoutedEventArgs e) => edit.SelectAll();

    // The toolbar's own commands and the app's buttons at its end (paste, pages).
    private void AddAppButtons()
    {
        bar.Undo.Click += OnUndo;
        bar.Redo.Click += OnRedo;
        bar.Cut.Click += OnRowCut;
        bar.Copy.Click += OnRowCopy;
        bar.Duplicate.Click += OnRowDuplicate;
        bar.DeleteSelection.Click += OnRowDelete;
        var paste = PenToolbar.Command(new SymbolIcon(Symbol.Paste),
            "Einfügen (Strg+V): Kopiertes oder einen Ausschnitt aus einer anderen App (Ausschneidetool: Win+Umschalt+S)", "PasteButton");
        paste.Click += OnPaste;
        // A page with a plus and a page with a minus (user, 2026-11-17: both in the row; deleting always asks first).
        var insertPage = PenToolbar.Command(PenToolbar.Path(
            "F0 M1,1 H11 V19 H1 Z M2.5,2.5 V17.5 H9.5 V2.5 Z " +
            "M15.25,9.5 H16.75 V13.25 H19.5 V14.75 H16.75 V18.5 H15.25 V14.75 H12.5 V13.25 H15.25 Z"),
            "Seite einfügen (nach der Seite im Blick)", "InsertPageButton");
        insertPage.Click += OnInsertPage;
        var deletePage = PenToolbar.Command(PenToolbar.Path(
            "F0 M1,1 H11 V19 H1 Z M2.5,2.5 V17.5 H9.5 V2.5 Z M12.5,13.25 H19.5 V14.75 H12.5 Z"),
            "Seite löschen (die Seite im Blick, mit Rückfrage)", "DeletePageButton");
        deletePage.Click += OnDeletePage;
        bar.Extensions.Add(paste);
        bar.Extensions.Add(new AppBarSeparator());
        bar.Extensions.Add(insertPage);
        bar.Extensions.Add(deletePage);
    }

    // The toolbar's controls hold the tools' choices (the workspace reads them); the settings keep them. Set before the
    // workspace is created, saved on each change.
    private void SetToolChoices()
    {
        bar.ShapeKind.SelectedIndex = (int)settings.Shape;
        bar.SelectMode.SelectedIndex = (int)settings.SelectMode;
        bar.EraserRadius.Value = settings.EraserRadius;
        bar.RecognizeShapes.IsChecked = settings.RecognizeShapes;
    }

    private void KeepToolSettings()
    {
        bar.ShapeKind.SelectionChanged += (_, _) => Save(() => settings.Shape = (ShapeKind)bar.ShapeKind.SelectedIndex, bar.ShapeKind.SelectedIndex >= 0);
        bar.SelectMode.SelectionChanged += (_, _) => Save(() => settings.SelectMode = (SelectMode)bar.SelectMode.SelectedIndex, bar.SelectMode.SelectedIndex >= 0);
        bar.EraserRadius.ValueChanged += (_, e) => Save(() => settings.EraserRadius = e.NewValue);
        ink.PartialErasingChanged += (_, _) => Save(() => settings.PartialErasing = ink.PartialErasing);
        bar.RecognizeShapes.Click += (_, _) => Save(() => settings.RecognizeShapes = bar.RecognizeShapes.IsChecked == true);
        ink.Tools.PresetsChanged += (_, _) => Save(() =>
        {
            settings.Pens = ink.Tools.PenSettings.Select(p => new PenSetting { ColorIndex = p.ColorIndex, Width = p.Width }).ToList();
            settings.Highlighters = ink.Tools.HighlighterSettings.Select(p => new PenSetting { ColorIndex = p.ColorIndex, Width = p.Width }).ToList();
        });

        void Save(Action change, bool valid = true)
        {
            if (valid)
            {
                change();
                settings.Save();
            }
        }
    }

    // ---------------------------------------------------------------- pages and view

    private void OnInsertPage(object sender, RoutedEventArgs e) => pages.InsertAfterCurrent();

    private async void OnDeletePage(object sender, RoutedEventArgs e)
    {
        var doc = session.Document;
        if (doc.Pages.Count <= 1)
        {
            messages.Show(InfoBarSeverity.Informational, "Seite nicht gelöscht", "Ein Dokument behält mindestens eine Seite.");
            return;
        }
        int index = pages.CurrentIndex;
        var page = doc.Pages[index];
        // Always asked (user, 2026-11-17): deleting a page cannot be undone (its history goes with it).
        string content = page.Strokes.Count > 0 || page.Images.Any() ? "Die Seite enthält Notizen. " : "";
        if (!await dialogs.ConfirmAsync($"Seite {index + 1} löschen?",
            content + "Das Löschen einer Seite lässt sich nicht rückgängig machen.", "Löschen"))
        {
            return;
        }
        pages.Remove(page);
    }

    private void OnFitWidth(object sender, RoutedEventArgs e) => pages.FitWidth();

    private void OnZoomIn(object sender, RoutedEventArgs e) => pages.Zoom(1.25f);

    private void OnZoomOut(object sender, RoutedEventArgs e) => pages.Zoom(0.8f);

    // ---------------------------------------------------------------- explorer

    /// <summary>The chosen folder, otherwise the git repository of the open document.</summary>
    private string? ProjectFolder
    {
        get
        {
            string? file = session.Document.FilePath;
            if (settings.ProjectFolder != null || file == null)
            {
                return settings.ProjectFolder;
            }
            try
            {
                return GitSync.FindRoot(Path.GetDirectoryName(file)!);
            }
            catch (Exception e)
            {
                DebugLog.Write("find git root", e);
                return null;
            }
        }
    }

    private async void RefreshExplorer()
    {
        if (sync.IsSyncing)
        {
            return; // git status would compete with the sync for the index; refreshed afterwards
        }
        string? folder = ProjectFolder;
        Tasks.Show(folder);
        await Explorer.ShowAsync(folder, session.OpenPath);
        SyncMenuItem.IsEnabled = Explorer.RepositoryRoot != null && !sync.IsSyncing;
    }

    private void OnPaneSelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
    {
        bool tasks = sender.SelectedItem == TasksTab;
        Tasks.Visibility = tasks ? Visibility.Visible : Visibility.Collapsed;
        Explorer.Visibility = tasks ? Visibility.Collapsed : Visibility.Visible;
        if (settings.TasksShown != tasks)
        {
            settings.TasksShown = tasks;
            settings.Save();
        }
    }

    private void SetExplorerOpen(bool open)
    {
        Split.IsPaneOpen = ExplorerMenuItem.IsChecked = open;
        ExplorerButton.IsChecked = open;
        if (settings.ExplorerOpen != open)
        {
            settings.ExplorerOpen = open;
            settings.Save();
        }
    }

    private void OnToggleExplorer(object sender, RoutedEventArgs e) => SetExplorerOpen(!Split.IsPaneOpen);

    private async void OnOpenFolder(object sender, RoutedEventArgs e)
    {
        var result = await new FolderPicker(AppWindow.Id).PickSingleFolderAsync();
        if (result == null)
        {
            return;
        }
        settings.ProjectFolder = result.Path;
        settings.Save();
        SetExplorerOpen(true);
        RefreshExplorer();
    }
}
