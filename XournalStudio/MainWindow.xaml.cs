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
/// <see cref="PagesController"/>, <see cref="SyncController"/>, <see cref="ExplorerCommands"/>, <see cref="InkTools"/>)
/// and holds no feature logic of its own.
/// </summary>
public sealed partial class MainWindow : Window
{
    private readonly AppSettings settings = AppSettings.Load();
    private readonly UndoStack undo = new();
    private readonly ToolController tools;
    private readonly InkTools inkTools;
    private readonly Messages messages;
    private readonly Dialogs dialogs;
    private readonly PagesController pages;
    private readonly DocumentSession session;
    private readonly SyncController sync;
    private readonly EditCommands edit;
    private readonly PdfExportCommand export;
    private readonly PartialEraser partialEraser;
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
        tools = new ToolController(Toolbar, PenButton, HighlighterButton);
        inkTools = new InkTools(undo, () => tools.PenAttributes);
        inkTools.Shapes.Shape = settings.Shape;
        inkTools.Selection.Mode = settings.SelectMode;
        pages = new PagesController(Scroller, PagesPanel, tools, inkTools, undo, () => RecognizeButton.IsChecked == true);
        dialogs = new Dialogs(() => Content.XamlRoot, () => pages.Views);
        session = new DocumentSession(AppWindow.Id, pages, dialogs, messages, settings, () => ProjectFolder);
        sync = new SyncController(session, dialogs, messages);
        var explorerCommands = new ExplorerCommands(Explorer, session, sync, dialogs, messages, FocusDocument, RefreshExplorer, index =>
        {
            Root.UpdateLayout(); // a document just opened: its pages are laid out before scrolling
            pages.ScrollToPage(index);
        });
        edit = new EditCommands(inkTools.Selection, pages, undo, messages, () => Toolbar.ActiveTool = SelectButton);
        var selectionCommands = new SelectionCommands(CopySelectionButton,
            [CutSelectionButton, DuplicateSelectionButton, DeleteSelectionButton]);
        export = new PdfExportCommand(AppWindow.Id, session, messages);
        inkTools.Selection.SelectionChanged += (_, _) => selectionCommands.ShowFor(inkTools.Selection.Current);

        ShapeChoice.SelectedIndex = (int)settings.Shape;
        SelectModeChoice.SelectedIndex = (int)settings.SelectMode;
        tools.CustomToolChanged += (_, _) => inkTools.Tool =
            tools.CustomTool == SelectButton ? InkTool.Select :
            tools.CustomTool == ShapeButton ? InkTool.Shape : InkTool.None;
        var eraser = new PartialEraser(undo, erased =>
        {
            // A selection must not keep strokes that are gone.
            var ids = erased.Select(s => s.Id).ToHashSet();
            if (inkTools.Selection.Current?.Strokes.Any(s => ids.Contains(s.Id)) == true)
            {
                inkTools.Selection.Clear();
            }
        }) { Radius = settings.EraserRadius };
        inkTools.EraserToolActive = () => tools.EraserActive;
        inkTools.ReleaseEraserEnd = () => tools.EraserEndTakenOver = false;
        EraserRadius.Value = settings.EraserRadius;
        PartialEraserButton.IsChecked = settings.PartialErasing;
        void ApplyEraserMode()
        {
            bool partial = PartialEraserButton.IsChecked == true;
            tools.EraserEndTakenOver = false;
            tools.PartialErasing = partial;
            inkTools.Eraser = partial ? eraser : null;
            if (!partial)
            {
                eraser.Hide();
            }
        }
        ApplyEraserMode();
        PartialEraserButton.Click += (_, _) =>
        {
            ApplyEraserMode();
            settings.PartialErasing = PartialEraserButton.IsChecked == true;
            settings.Save();
        };
        partialEraser = eraser;
        RecognizeButton.IsChecked = settings.RecognizeShapes;
        RecognizeButton.Click += (_, _) =>
        {
            settings.RecognizeShapes = RecognizeButton.IsChecked == true;
            settings.Save();
        };
        undo.Changed += (_, _) =>
        {
            UndoButton.IsEnabled = UndoMenuItem.IsEnabled = undo.CanUndo;
            RedoButton.IsEnabled = RedoMenuItem.IsEnabled = undo.CanRedo;
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
        inkTools.Selection.Clear(); // undo may replace the selected strokes
        undo.Undo();
    }

    private void OnRedo(object sender, RoutedEventArgs e)
    {
        if (edit.RefusedWhileDragging("Wiederholen"))
        {
            return;
        }
        inkTools.Selection.Clear();
        undo.Redo();
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

    private void OnShapeChoiceChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ShapeChoice.SelectedIndex >= 0 && inkTools != null)
        {
            inkTools.Shapes.Shape = settings.Shape = (ShapeKind)ShapeChoice.SelectedIndex;
            settings.Save();
        }
    }

    private void OnEraserRadiusChanged(object sender, Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (partialEraser != null)
        {
            partialEraser.Radius = settings.EraserRadius = e.NewValue;
            settings.Save();
        }
    }

    private void OnSelectModeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (SelectModeChoice.SelectedIndex >= 0 && inkTools != null)
        {
            inkTools.Selection.Mode = settings.SelectMode = (SelectMode)SelectModeChoice.SelectedIndex;
            settings.Save();
        }
    }

    // ---------------------------------------------------------------- pages and view

    private void OnInsertPage(object sender, RoutedEventArgs e) => pages.InsertAfterCurrent();

    private async void OnDeletePage(object sender, RoutedEventArgs e)
    {
        var doc = session.Document;
        if (doc.Pages.Count <= 1)
        {
            return;
        }
        int index = pages.CurrentIndex;
        var page = doc.Pages[index];
        if ((page.Strokes.Count > 0 || page.Images.Any()) &&
            !await dialogs.ConfirmAsync($"Seite {index + 1} löschen?", "Die Seite enthält Notizen.", "Löschen"))
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
