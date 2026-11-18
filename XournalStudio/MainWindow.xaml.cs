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
    private readonly PdfImportCommand import;
    private readonly PageThumbnails thumbnails;
    private readonly PageCommands pageCommands;
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
        AppWindow.TitleBar.PreferredTheme = TitleBarTheme.UseDefaultAppMode; // a dark title bar in dark mode (default: Legacy = light)

        messages = new Messages(Info);
        InkLog.Writer = DebugLog.Write;
        InkLog.ErrorWriter = message => DebugLog.Error($"ink error: {message}");
        bar = AppToolbar.Create(settings);
        ToolbarHost.Content = bar;
        ink = new InkWorkspace(bar, Scroller);
        AddAppButtons();
        ink.ChoicesChanged += (_, _) =>
        {
            settings.Keep(ink.Choices);
            settings.Save();
        };
        pages = new PagesController(Scroller, PagesPanel, ink);
        dialogs = new Dialogs(() => Content.XamlRoot, pages);
        session = new DocumentSession(AppWindow.Id, pages, dialogs, messages, settings);
        sync = new SyncController(session, dialogs, messages);
        var explorerCommands = new ExplorerCommands(Explorer, session, dialogs, messages, FocusDocument, Explorer.RefreshSoon, index =>
        {
            Root.UpdateLayout(); // a document just opened: its pages are laid out before scrolling
            pages.ScrollToPage(index);
        });
        edit = new EditCommands(ink.Commands, ink.Selection, pages, ink.Undo, messages);
        ink.Commands.Refused += (_, command) => edit.ReportRefused(command);
        ink.Selection.TransferFailed += edit.ReportTransferFailed;
        export = new PdfExportCommand(AppWindow.Id, session, messages);
        import = new PdfImportCommand(AppWindow.Id, session, dialogs, messages);
        thumbnails = new PageThumbnails(session, PagesList, pages);
        pageCommands = new PageCommands(pages, session, ink, PagesList, thumbnails,
            new InsertPdfPagesCommand(AppWindow.Id, session, pages, dialogs, messages), dialogs, messages);
        ink.Undo.Changed += (_, _) =>
        {
            UndoMenuItem.IsEnabled = ink.Undo.CanUndo;
            RedoMenuItem.IsEnabled = ink.Undo.CanRedo;
        };
        // Only real edits of the ink (recorded, undone, redone) – not a cleared history when another document opens.
        ink.Undo.ContentChanged += (_, _) => session.MarkChanged();
        pages.PagesChanged += (_, _) => session.MarkChanged();
        session.StateChanged += (_, _) => Title = $"{session.Title} – Xournal Studio";
        session.FilesChanged += (_, _) => Explorer.RefreshSoon();
        sync.StateChanged += (_, _) =>
        {
            UpdateSyncMenu();
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
        Explorer.Messages = messages;
        SetExplorerOpen(settings.ExplorerOpen);

        Tasks.Initialize(dialogs, messages, () => (session.Document.FilePath, pages.CurrentIndex),
            () => session.RefusedWhileSyncing("Das Ändern von Aufgaben"));
        Tasks.ShowsDone = settings.DoneTasksShown;
        Tasks.ShowsDoneChanged += (_, _) =>
        {
            settings.DoneTasksShown = Tasks.ShowsDone;
            settings.Save();
        };
        Tasks.OpenRequested += async (_, link) => await explorerCommands.OpenDocumentAsync(link.Document, link.PageIndex);
        (settings.PagesShown ? PagesTab : settings.TasksShown ? TasksTab : FilesTab).IsSelected = true;

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
            Explorer.RefreshSoon(); // files may have changed outside the app (refreshed after a sync anyway)
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
        settings.Keep(ink.Choices); // a choice of the last moment (ChoicesChanged waits until the choices settle)
        settings.Save();
        await session.EndAsync(); // the recovery copy is gone before the process ends
        await thumbnails.CloseAsync(); // its temporary PDF (the user's notes) too
        await Services.PdfRenderer.Shared.ShutdownAsync(); // its GPU device goes while the app still runs (see there)
        Close();
    }

    // ---------------------------------------------------------------- file

    private async void OnNew(object sender, RoutedEventArgs e) => await session.LeaveAndShowNewAsync();

    private async void OnOpen(object sender, RoutedEventArgs e)
    {
        if (await Pickers.OpenAsync(AppWindow.Id, Pickers.Document, Pickers.Pdf) is { } path)
        {
            await session.LeaveAndOpenAsync(path);
        }
    }

    private async void OnSave(object sender, RoutedEventArgs e) => await session.SaveOrSaveAsAsync();

    private async void OnSaveAs(object sender, RoutedEventArgs e) => await session.SaveAsAsync();

    private void OnSync(object sender, RoutedEventArgs e) => sync.Sync(Explorer.RepositoryRoot);

    private async void OnExportPdf(object sender, RoutedEventArgs e) => await export.ExportAsync();

    private async void OnImportPdf(object sender, RoutedEventArgs e) => await import.ImportAsync();

    // ---------------------------------------------------------------- edit

    private void OnUndo(object sender, RoutedEventArgs e) => ink.Commands.Undo(); // refused while dragging (said)

    private void OnRedo(object sender, RoutedEventArgs e) => ink.Commands.Redo();

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

    private void OnRowPaste(object sender, RoutedEventArgs e) { FocusDocument(); OnPaste(sender, e); }

    private void OnSelectAll(object sender, RoutedEventArgs e) => edit.SelectAll();

    // The toolbar's own commands (undo, redo, delete run the library's; cut, copy, duplicate and paste the file's own
    // clipboard format, so not Windows ink's) and the app's page buttons at its end; the menu shows the same icons.
    private void AddAppButtons()
    {
        ink.Commands.WireToolbar(clipboard: false);
        bar.Undo.Click += (_, _) => FocusDocument();
        bar.Redo.Click += (_, _) => FocusDocument();
        bar.DeleteSelection.Click += (_, _) => FocusDocument();
        bar.Cut.Click += OnRowCut;
        bar.Copy.Click += OnRowCopy;
        bar.Duplicate.Click += OnRowDuplicate;
        bar.Paste.Click += OnRowPaste;
        bar.Paste.IsEnabled = true; // always: pictures, the file's own format and ink of other apps paste too
        var (insertPage, deletePage) = AppToolbar.AddButtons(bar);
        insertPage.Click += OnInsertPage;
        deletePage.Click += OnDeletePage;
        DuplicateMenuItem.Icon = PenToolbar.Path(PenToolbar.DuplicateIcon);
        InsertPageMenuItem.Icon = PenToolbar.Path(AppToolbar.InsertPageIcon);
        DeletePageMenuItem.Icon = PenToolbar.Path(AppToolbar.DeletePageIcon);
    }

    // ---------------------------------------------------------------- pages and view

    private void OnInsertPage(object sender, RoutedEventArgs e) => pageCommands.InsertAfterCurrent();

    private async void OnInsertPdfPages(object sender, RoutedEventArgs e) => await pageCommands.InsertPdfPagesAsync();

    private async void OnDeletePage(object sender, RoutedEventArgs e) => await pageCommands.DeleteCurrentAsync();

    private void OnFitWidth(object sender, RoutedEventArgs e) => pages.FitWidth();

    private void OnZoomIn(object sender, RoutedEventArgs e) => pages.Zoom(1.25f);

    private void OnZoomOut(object sender, RoutedEventArgs e) => pages.Zoom(0.8f);

    // ---------------------------------------------------------------- explorer

    private async void RefreshExplorer()
    {
        if (sync.IsSyncing)
        {
            return; // git status would compete with the sync for the index; refreshed afterwards
        }
        string? folder = session.ProjectFolder;
        Tasks.Show(folder);
        await Explorer.ShowAsync(folder, session.OpenPath);
        UpdateSyncMenu();
    }

    private void UpdateSyncMenu() => SyncMenuItem.IsEnabled = Explorer.RepositoryRoot != null && !sync.IsSyncing;

    private void OnPaneSelectionChanged(SelectorBar sender, SelectorBarSelectionChangedEventArgs args)
    {
        bool tasks = sender.SelectedItem == TasksTab, pagesShown = sender.SelectedItem == PagesTab;
        Tasks.Visibility = tasks ? Visibility.Visible : Visibility.Collapsed;
        PagesList.Visibility = pagesShown ? Visibility.Visible : Visibility.Collapsed;
        Explorer.Visibility = tasks || pagesShown ? Visibility.Collapsed : Visibility.Visible;
        if (thumbnails != null)
        {
            thumbnails.Active = pagesShown && Split.IsPaneOpen;
        }
        if (settings.TasksShown != tasks || settings.PagesShown != pagesShown)
        {
            settings.TasksShown = tasks;
            settings.PagesShown = pagesShown;
            settings.Save();
        }
    }

    private void SetExplorerOpen(bool open)
    {
        Split.IsPaneOpen = ExplorerMenuItem.IsChecked = open;
        if (thumbnails != null)
        {
            thumbnails.Active = open && PagesTab.IsSelected;
        }
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
        if (await Pickers.FolderAsync(AppWindow.Id) is not { } folder)
        {
            return;
        }
        settings.ProjectFolder = folder;
        settings.Save();
        SetExplorerOpen(true);
        RefreshExplorer();
    }
}
