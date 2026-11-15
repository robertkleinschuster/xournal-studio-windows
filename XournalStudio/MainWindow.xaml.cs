using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using XournalStudio.Views;

namespace XournalStudio;

public sealed partial class MainWindow : Window
{
    private readonly AppSettings settings = AppSettings.Load();
    private readonly UndoStack undo = new();
    private readonly ToolController tools;
    private readonly InkTools inkTools;
    private readonly DispatcherQueueTimer viewTimer;

    private XoppDocument document = XoppDocument.CreateNew();
    private PdfBackground? pdf;
    private bool dirty;
    private bool fitWidthPending = true;
    private bool closeConfirmed;

    private IEnumerable<PageView> PageViews => PagesPanel.Children.OfType<PageView>();

    public MainWindow(string? fileToOpen)
    {
        InitializeComponent();
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico"));
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.Maximize();
        }

        tools = new ToolController(Toolbar, PenButton, HighlighterButton);
        inkTools = new InkTools(undo, () => tools.PenAttributes) { Shape = settings.Shape };
        inkTools.Message += (_, m) => ShowMessage(m.Severity, m.Title, m.Message);
        inkTools.BarrelTapped += (_, _) => tools.ReturnFromLasso();
        ShapeChoice.SelectedIndex = (int)settings.Shape;
        tools.CustomToolChanged += (_, _) => inkTools.Tool =
            tools.CustomTool == LassoButton ? InkTool.Lasso :
            tools.CustomTool == ShapeButton ? InkTool.Shape :
            tools.CustomTool == PdfAreaButton ? InkTool.PdfArea : InkTool.None;
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
        };
        undo.Changed += (_, _) => SetDirty(true);

        viewTimer = DispatcherQueue.CreateTimer();
        viewTimer.Interval = TimeSpan.FromMilliseconds(120);
        viewTimer.IsRepeating = false;
        viewTimer.Tick += (_, _) => UpdateVisiblePages();

        Explorer.FileInvoked += OnExplorerFileInvoked;
        Explorer.SyncRequested += (_, _) => Sync();
        Explorer.CommandRequested += OnExplorerCommand;
        Explorer.FolderChanged += (_, _) => RefreshExplorer();
        Explorer.Failed += (_, m) => ShowMessage(InfoBarSeverity.Warning, m.Title, m.Message);
        SetExplorerOpen(settings.ExplorerOpen);

        Activated += OnActivated;
        AppWindow.Closing += OnClosing;

        string? file = fileToOpen ?? settings.LastFile;
        if (file != null && File.Exists(file))
        {
            _ = OpenAsync(file);
        }
        else
        {
            ShowDocument(XoppDocument.CreateNew(), null);
        }
    }

    // ---------------------------------------------------------------- document

    private async Task OpenAsync(string path)
    {
        try
        {
            if (Path.GetExtension(path).Equals(".pdf", StringComparison.OrdinalIgnoreCase))
            {
                // Annotations of "x.pdf" live in "x.xopp" next to it, created on the first save.
                string xopp = Path.ChangeExtension(path, ".xopp");
                if (File.Exists(xopp))
                {
                    await OpenAsync(xopp);
                    return;
                }
                var pdfBackground = await PdfBackground.LoadAsync(path);
                var doc = XoppDocument.FromPdf(path, pdfBackground.GetPageSizes());
                doc.FilePath = xopp;
                ShowDocument(doc, pdfBackground);
            }
            else
            {
                var doc = XoppDocument.Load(path);
                PdfBackground? pdfBackground = null;
                if (doc.PdfPath != null)
                {
                    try
                    {
                        pdfBackground = await PdfBackground.LoadAsync(doc.PdfPath);
                    }
                    catch (Exception e)
                    {
                        DebugLog.Write("load PDF background", e);
                        ShowMessage(InfoBarSeverity.Warning, "PDF-Hintergrund nicht gefunden", $"{doc.PdfPath}: {DebugLog.Describe(e)}");
                    }
                }
                ShowDocument(doc, pdfBackground);
            }
            settings.LastFile = path;
            settings.Save();
        }
        catch (Exception e)
        {
            DebugLog.Write("open", e);
            ShowMessage(InfoBarSeverity.Error, "Datei konnte nicht geöffnet werden", $"{path}: {DebugLog.Describe(e)}");
        }
    }

    private void ShowDocument(XoppDocument doc, PdfBackground? pdfBackground)
    {
        document = doc;
        pdf = pdfBackground;
        inkTools.ClearSelection();
        undo.Clear();
        PagesPanel.Children.Clear();
        foreach (var page in doc.Pages)
        {
            PagesPanel.Children.Add(CreatePageView(page));
        }
        OnPagesChanged();
        SetDirty(false);
        RefreshExplorer();
        fitWidthPending = true;
        Scroller.ScrollTo(0, 0, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        FitWidthWhenReady();
    }

    private PageView CreatePageView(XoppPage page)
    {
        var view = new PageView(page, pdf);
        inkTools.Attach(view);
        tools.Attach(view, LassoButton);
        view.Ink.StrokesCollected += (_, e) => OnStrokesCollected(view, e.Strokes);
        view.Ink.StrokesErased += (_, e) =>
        {
            var edit = new PageEdit(page);
            edit.RemovedStrokes.AddRange(e.Strokes);
            undo.Push(edit);
        };
        return view;
    }

    /// <summary>Records new ink for undo; with shape recognition on, hand-drawn shapes become clean ones.</summary>
    private async void OnStrokesCollected(PageView view, IReadOnlyList<Windows.UI.Input.Inking.InkStroke> strokes)
    {
        var page = view.Page;
        var edit = new PageEdit(page);
        foreach (var stroke in strokes.ToList())
        {
            var shape = RecognizeButton.IsChecked == true && !stroke.DrawingAttributes.DrawAsHighlighter
                ? await ShapeRecognition.RecognizeAsync(stroke) : null;
            if (shape != null && page.Ink!.GetStrokes().Contains(stroke))
            {
                PageEdit.RemoveStrokes(page, [stroke]);
                page.Ink!.AddStroke(shape);
                edit.AddedStrokes.Add(shape);
            }
            else
            {
                edit.AddedStrokes.Add(stroke);
            }
        }
        undo.Push(edit);
    }

    private void OnPagesChanged()
    {
        tools.SetPages(PageViews);
        viewTimer.Start();
    }

    private bool Save()
    {
        if (document.FilePath == null)
        {
            return false;
        }
        if (syncing)
        {
            // Git is committing and checking out the working tree right now.
            ShowMessage(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Gespeichert wird, sobald die Synchronisierung fertig ist.");
            return false;
        }
        try
        {
            document.Save(document.FilePath);
            SetDirty(false);
            settings.LastFile = document.FilePath;
            settings.Save();
            RefreshExplorer();
            return true;
        }
        catch (Exception e)
        {
            DebugLog.Write("save", e);
            ShowMessage(InfoBarSeverity.Error, "Speichern fehlgeschlagen", $"{document.FilePath}: {DebugLog.Describe(e)}");
            return false;
        }
    }

    private async Task<bool> SaveAsAsync()
    {
        var picker = new FileSavePicker(AppWindow.Id)
        {
            SuggestedFileName = Path.GetFileNameWithoutExtension(document.FilePath ?? "Notizen"),
        };
        picker.FileTypeChoices.Add("Xournal++-Dokument", [".xopp"]);
        var result = await picker.PickSaveFileAsync();
        if (result == null)
        {
            return false;
        }
        document.FilePath = result.Path;
        return Save();
    }

    private void SetDirty(bool value)
    {
        dirty = value;
        string name = document.FilePath != null ? Path.GetFileName(document.FilePath) : "Unbenannt";
        Title = $"{(dirty ? "• " : "")}{name} – Xournal Studio";
    }

    // ---------------------------------------------------------------- view

    private void FitWidthWhenReady()
    {
        if (Scroller.ViewportWidth <= 0 || document.Pages.Count == 0)
        {
            return;
        }
        fitWidthPending = false;
        double pageWidth = document.Pages.Max(p => p.Width) + 32;
        float zoom = (float)Math.Clamp(Scroller.ViewportWidth / pageWidth, Scroller.MinZoomFactor, Scroller.MaxZoomFactor);
        Scroller.ZoomTo(zoom, System.Numerics.Vector2.Zero, new ScrollingZoomOptions(ScrollingAnimationMode.Disabled));

        // Test hook: start at page XOURNAL_STUDIO_TEST_PAGE (1-based).
        if (int.TryParse(Environment.GetEnvironmentVariable("XOURNAL_STUDIO_TEST_PAGE"), out int testPage) &&
            testPage >= 1 && testPage <= PagesPanel.Children.Count)
        {
            PagesPanel.UpdateLayout();
            double y = PagesPanel.Children[testPage - 1].ActualOffset.Y * zoom;
            Scroller.ScrollTo(0, y, new ScrollingScrollOptions(ScrollingAnimationMode.Disabled));
        }
    }

    private void Zoom(float factor)
    {
        var center = new System.Numerics.Vector2((float)Scroller.ViewportWidth / 2, (float)Scroller.ViewportHeight / 2);
        Scroller.ZoomTo((float)Math.Clamp(Scroller.ZoomFactor * factor, Scroller.MinZoomFactor, Scroller.MaxZoomFactor), center);
    }

    private void OnScrollerSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (fitWidthPending)
        {
            FitWidthWhenReady();
        }
        viewTimer.Start();
    }

    private void OnViewChanged(ScrollView sender, object args) => viewTimer.Start();

    /// <summary>Renders PDF backgrounds of pages near the viewport at the current zoom; frees the others.</summary>
    private void UpdateVisiblePages()
    {
        var viewport = new Rect(0, 0, Scroller.ViewportWidth, Scroller.ViewportHeight);
        var near = new Rect(0, -viewport.Height, viewport.Width, viewport.Height * 3);
        double scale = Scroller.ZoomFactor * (Content.XamlRoot?.RasterizationScale ?? 1.0);
        DebugLog.Write($"zoom={Scroller.ZoomFactor} offset={Scroller.HorizontalOffset},{Scroller.VerticalOffset} " +
            $"extent={Scroller.ExtentWidth}x{Scroller.ExtentHeight} viewport={Scroller.ViewportWidth}x{Scroller.ViewportHeight} " +
            $"panel={PagesPanel.ActualWidth}x{PagesPanel.ActualHeight}");
        foreach (var view in PageViews)
        {
            var bounds = view.TransformToVisual(Scroller).TransformBounds(new Rect(0, 0, view.Width, view.Height));
            view.UpdateBackground(Intersects(bounds, near), scale);
        }
        if (PagesPanel.Children.Count > 0)
        {
            tools.SetCurrentPage(PagesPanel.Children[CurrentPageIndex()] as PageView);
        }
    }

    /// <summary>The page in the middle of the viewport (or the last one above it).</summary>
    private int CurrentPageIndex()
    {
        double middle = Scroller.ViewportHeight / 2;
        int index = 0, i = 0;
        foreach (var view in PageViews)
        {
            var bounds = view.TransformToVisual(Scroller).TransformBounds(new Rect(0, 0, view.Width, view.Height));
            if (bounds.Top <= middle)
            {
                index = i;
            }
            i++;
        }
        return index;
    }

    private static bool Intersects(Rect a, Rect b) =>
        a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;

    private void ShowMessage(InfoBarSeverity severity, string title, string message)
    {
        DebugLog.Write($"{severity}: {title}: {message}");
        Info.Severity = severity;
        Info.Title = title;
        Info.Message = message;
        Info.IsOpen = true;
    }

    // ---------------------------------------------------------------- window

    /// <summary>
    /// Shows a dialog with the pages' ink hidden. The experimental InkCanvas (2.6.6-experimental) renders and takes
    /// pointer input above XAML popups, so mouse and pen clicks on dialog buttons went to the ink. Remove when the
    /// Windows App SDK fixes it.
    /// </summary>
    private async Task<ContentDialogResult> ShowDialogAsync(ContentDialog dialog)
    {
        var views = PageViews.ToList();
        foreach (var view in views)
        {
            view.InkCanvas.Visibility = Visibility.Collapsed;
        }
        try
        {
            return await dialog.ShowAsync();
        }
        finally
        {
            foreach (var view in views)
            {
                view.InkCanvas.Visibility = Visibility.Visible;
            }
        }
    }

    private void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        // Like Xournal Studio: save when the window loses focus.
        if (syncing)
        {
            return; // saved and refreshed when the sync is done
        }
        if (args.WindowActivationState == WindowActivationState.Deactivated && dirty && document.FilePath != null)
        {
            Save();
        }
        else if (args.WindowActivationState != WindowActivationState.Deactivated)
        {
            RefreshExplorer(); // files may have changed outside the app
        }
    }

    private async void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (closeConfirmed || !dirty)
        {
            return;
        }
        if (document.FilePath != null)
        {
            if (Save())
            {
                return;
            }
            args.Cancel = true;
            return;
        }

        args.Cancel = true;
        var dialog = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = "Änderungen speichern?",
            Content = "Das Dokument wurde noch nicht gespeichert.",
            PrimaryButtonText = "Speichern",
            SecondaryButtonText = "Nicht speichern",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        };
        var choice = await ShowDialogAsync(dialog);
        if (choice == ContentDialogResult.None || (choice == ContentDialogResult.Primary && !await SaveAsAsync()))
        {
            return;
        }
        closeConfirmed = true;
        Close();
    }

    // ---------------------------------------------------------------- commands

    private async void OnNew(object sender, RoutedEventArgs e)
    {
        if (await SaveBeforeReplacingAsync())
        {
            ShowDocument(XoppDocument.CreateNew(), null);
        }
    }

    private async void OnOpen(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker(AppWindow.Id);
        picker.FileTypeFilter.Add(".xopp");
        picker.FileTypeFilter.Add(".pdf");
        var result = await picker.PickSingleFileAsync();
        if (result != null && await SaveBeforeReplacingAsync())
        {
            await OpenAsync(result.Path);
        }
    }

    private async void OnSave(object sender, RoutedEventArgs e)
    {
        if (document.FilePath == null)
        {
            await SaveAsAsync();
        }
        else
        {
            Save();
        }
    }

    private async void OnSaveAs(object sender, RoutedEventArgs e) => await SaveAsAsync();

    /// <summary>Saves a modified document before another one replaces it; false = keep it open.</summary>
    private async Task<bool> SaveBeforeReplacingAsync()
    {
        if (!dirty)
        {
            return true;
        }
        return document.FilePath != null ? Save() : await SaveAsAsync();
    }

    private void OnUndo(object sender, RoutedEventArgs e)
    {
        inkTools.ClearSelection(); // undo may replace the selected strokes
        undo.Undo();
    }

    private void OnRedo(object sender, RoutedEventArgs e)
    {
        inkTools.ClearSelection();
        undo.Redo();
    }

    private void OnCopy(object sender, RoutedEventArgs e) => inkTools.CopySelection();

    private void OnDeleteSelection(object sender, RoutedEventArgs e) => inkTools.DeleteSelection();

    private void OnShapeChoiceChanged(object sender, SelectionChangedEventArgs e)
    {
        if (ShapeChoice.SelectedIndex >= 0 && inkTools != null)
        {
            inkTools.Shape = settings.Shape = (ShapeKind)ShapeChoice.SelectedIndex;
            settings.Save();
        }
    }

    private void OnInsertPage(object sender, RoutedEventArgs e)
    {
        int index = document.Pages.Count == 0 ? -1 : CurrentPageIndex();
        var current = index >= 0 ? document.Pages[index] : null;
        var page = XoppPage.CreateGraph(current?.Width ?? XoppDocument.A4Width, current?.Height ?? XoppDocument.A4Height);
        document.Pages.Insert(index + 1, page);
        PagesPanel.Children.Insert(index + 1, CreatePageView(page));
        OnPagesChanged();
        SetDirty(true);
    }

    private async void OnDeletePage(object sender, RoutedEventArgs e)
    {
        if (document.Pages.Count <= 1)
        {
            return;
        }
        int index = CurrentPageIndex();
        var page = document.Pages[index];
        if (page.Strokes.Count > 0)
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Content.XamlRoot,
                Title = $"Seite {index + 1} löschen?",
                Content = "Die Seite enthält Notizen.",
                PrimaryButtonText = "Löschen",
                CloseButtonText = "Abbrechen",
                DefaultButton = ContentDialogButton.Close,
            };
            if (await ShowDialogAsync(dialog) != ContentDialogResult.Primary)
            {
                return;
            }
        }
        document.Pages.RemoveAt(index);
        PagesPanel.Children.RemoveAt(index);
        inkTools.ClearSelection();
        undo.Forget(page);
        OnPagesChanged();
        SetDirty(true);
    }

    private void OnFitWidth(object sender, RoutedEventArgs e) => FitWidthWhenReady();

    private void OnZoomIn(object sender, RoutedEventArgs e) => Zoom(1.25f);

    private void OnZoomOut(object sender, RoutedEventArgs e) => Zoom(0.8f);

    // ---------------------------------------------------------------- explorer

    private bool syncing;

    /// <summary>The chosen folder, otherwise the git repository of the open document.</summary>
    private string? ProjectFolder
    {
        get
        {
            if (settings.ProjectFolder != null || document.FilePath == null)
            {
                return settings.ProjectFolder;
            }
            try
            {
                return GitSync.FindRoot(Path.GetDirectoryName(document.FilePath)!);
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
        if (syncing)
        {
            return; // git status would compete with the sync for the index; refreshed afterwards
        }
        await Explorer.ShowAsync(ProjectFolder, document.FilePath);
        SyncMenuItem.IsEnabled = Explorer.RepositoryRoot != null && !syncing;
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

    private async void OnExplorerFileInvoked(object? sender, string path)
    {
        bool isOpen = string.Equals(path, document.FilePath, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(Path.ChangeExtension(path, ".xopp"), document.FilePath, StringComparison.OrdinalIgnoreCase);
        if (!isOpen && !syncing && await SaveBeforeReplacingAsync())
        {
            await OpenAsync(path);
        }
    }

    // ---------------------------------------------------------------- explorer: file commands

    /// <summary>New document/folder, rename, delete from the explorer's context menu or keys (F2, Del).</summary>
    private async void OnExplorerCommand(object? sender, ExplorerCommandRequest request)
    {
        if (syncing)
        {
            ShowMessage(InfoBarSeverity.Informational, "Synchronisierung läuft",
                "Dateien und Ordner können geändert werden, sobald die Synchronisierung fertig ist.");
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
            }
        }
        catch (Exception e)
        {
            DebugLog.Write($"explorer {request.Command} {request.Path}", e);
            string title = request.Command switch
            {
                ExplorerCommand.NewDocument => "Dokument konnte nicht erstellt werden",
                ExplorerCommand.NewFolder => "Ordner konnte nicht erstellt werden",
                ExplorerCommand.Rename => "Umbenennen fehlgeschlagen",
                _ => "Löschen fehlgeschlagen",
            };
            ShowMessage(InfoBarSeverity.Error, title, $"{request.Path}: {DebugLog.Describe(e)}");
        }
        RefreshExplorer();
    }

    /// <summary>A new, empty document (the same as Datei → Neu) saved in <paramref name="folder"/> and opened.</summary>
    private async Task NewDocumentAsync(string folder)
    {
        string? name = await AskNameAsync("Neues Dokument",
            Path.GetFileNameWithoutExtension(ProjectFiles.UniqueName(folder, "Neues Dokument", ProjectFiles.DocumentExtension)),
            "Erstellen", n => ValidateNewName(folder, n + ProjectFiles.DocumentExtension, null));
        if (name == null || !await SaveBeforeReplacingAsync())
        {
            return;
        }
        string path = Path.Combine(folder, name + ProjectFiles.DocumentExtension);
        XoppDocument.CreateNew().Save(path);
        await OpenAsync(path);
    }

    private async Task NewFolderAsync(string folder)
    {
        string? name = await AskNameAsync("Neuer Ordner", ProjectFiles.UniqueName(folder, "Neuer Ordner"), "Erstellen",
            n => ValidateNewName(folder, n, null));
        if (name != null)
        {
            Explorer.SelectAfterRefresh(await ProjectFiles.CreateFolderAsync(folder, name));
        }
    }

    /// <summary>
    /// Renames a file (only the name before the extension is edited; a .xopp keeps referring to its PDF, which stays)
    /// or a folder. The open document, if renamed with it, is saved before and opened again from its new place.
    /// </summary>
    private async Task RenameAsync(string path, bool isFolder)
    {
        string parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(path))!;
        string? edited = await AskNameAsync("Umbenennen", ProjectFiles.EditableName(path, isFolder), "Umbenennen",
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
        bool affectsOpen = ProjectFiles.IsAtOrBelow(document.FilePath, path) || ProjectFiles.IsAtOrBelow(document.PdfPath, path);
        // A document made from a PDF that has no .xopp yet stays open with its notes; it follows the PDF.
        bool unsavedFromPdf = affectsOpen && document.FilePath != null && !File.Exists(document.FilePath);
        if (affectsOpen && !unsavedFromPdf && !await SaveBeforeReplacingAsync())
        {
            return;
        }
        string renamed = await ProjectFiles.RenameAsync(path, isFolder, newName);
        Explorer.SelectAfterRefresh(renamed);
        if (unsavedFromPdf)
        {
            document.FilePath = ProjectFiles.MapRenamed(document.FilePath!, path, renamed);
            if (document.PdfPath != null)
            {
                document.MovePdf(ProjectFiles.MapRenamed(document.PdfPath, path, renamed));
                // Annotations of "x.pdf" go to "x.xopp" (see OpenAsync) – unless that is another document.
                string xopp = Path.ChangeExtension(document.PdfPath, ProjectFiles.DocumentExtension);
                if (!File.Exists(xopp))
                {
                    document.FilePath = xopp;
                }
            }
            settings.LastFile = document.PdfPath;
            settings.Save();
            SetDirty(dirty); // the title shows the new name
        }
        else if (affectsOpen && document.FilePath != null)
        {
            await OpenAsync(ProjectFiles.MapRenamed(document.FilePath, path, renamed));
        }
    }

    /// <summary>
    /// Moves a file or folder to the recycle bin after asking. The open document, if deleted with it, is saved first
    /// (so the recycle bin holds the latest notes) and closed.
    /// </summary>
    private async Task DeleteAsync(string path, bool isFolder)
    {
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(path));
        string content = isFolder
            ? "Der Ordner wird mit allem, was darin ist, in den Papierkorb verschoben."
            : "Die Datei wird in den Papierkorb verschoben.";
        string pdfOfDocument = Path.ChangeExtension(path, ".pdf");
        if (!isFolder && path.EndsWith(ProjectFiles.DocumentExtension, StringComparison.OrdinalIgnoreCase) &&
            File.Exists(pdfOfDocument))
        {
            content += $" Das PDF „{Path.GetFileName(pdfOfDocument)}“ bleibt (ohne Notizen).";
        }
        var dialog = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = $"„{name}“ löschen?",
            Content = content,
            PrimaryButtonText = "Löschen",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Close,
        };
        if (await ShowDialogAsync(dialog) != ContentDialogResult.Primary)
        {
            return;
        }
        bool affectsOpen = ProjectFiles.IsAtOrBelow(document.FilePath, path) || ProjectFiles.IsAtOrBelow(document.PdfPath, path);
        if (affectsOpen)
        {
            // Not when the .xopp does not exist yet: saving would create it just as its PDF goes.
            if (dirty && document.FilePath != null && File.Exists(document.FilePath) && !Save())
            {
                return;
            }
            ShowDocument(XoppDocument.CreateNew(), null);
        }
        await ProjectFiles.DeleteAsync(path, isFolder);
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

    /// <summary>Asks for a name; the button is enabled only while <paramref name="validate"/> finds no problem.</summary>
    private async Task<string?> AskNameAsync(string title, string initial, string primary, Func<string, string?> validate)
    {
        var box = new TextBox { Text = initial, Header = "Name" };
        var dialog = new ContentDialog
        {
            XamlRoot = Content.XamlRoot,
            Title = title,
            Content = box,
            PrimaryButtonText = primary,
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        };
        void Validate()
        {
            string? error = validate(box.Text);
            box.Description = error;
            dialog.IsPrimaryButtonEnabled = error == null;
        }
        box.TextChanged += (_, _) => Validate();
        dialog.Opened += (_, _) =>
        {
            box.Focus(FocusState.Programmatic);
            box.SelectAll();
        };
        Validate();
        return await ShowDialogAsync(dialog) == ContentDialogResult.Primary ? box.Text : null;
    }

    /// <summary>Saves, then commits everything, pulls with rebase and pushes; reloads the document if the pull changed it.</summary>
    private void OnSync(object sender, RoutedEventArgs e) => Sync();

    private async void Sync()
    {
        string? root = Explorer.RepositoryRoot;
        if (syncing || root == null)
        {
            return;
        }
        if (dirty && document.FilePath != null && !Save())
        {
            return;
        }
        syncing = true;
        Explorer.IsSyncing = true;
        SyncMenuItem.IsEnabled = false;
        try
        {
            var result = await Task.Run(() =>
                GitSync.Sync(root, GitCredentials.Get, Environment.MachineName, DateTimeOffset.Now));
            bool documentChanged = result.PulledFiles.Any(f =>
                SamePath(f, document.FilePath) || SamePath(f, document.PdfPath));
            if (!documentChanged || await ResolvePulledDocumentAsync())
            {
                ShowMessage(InfoBarSeverity.Success, "Synchronisiert", Describe(result));
            }
        }
        catch (GitSyncConflictException ex)
        {
            DebugLog.Write("sync conflict", ex);
            ShowMessage(InfoBarSeverity.Error, "Konflikt beim Synchronisieren",
                $"{ex.Message}. Die eigenen Änderungen sind committet, aber nicht hochgeladen; den Konflikt mit Git lösen.");
        }
        catch (Exception ex)
        {
            DebugLog.Write("sync", ex);
            ShowMessage(InfoBarSeverity.Error, "Synchronisieren fehlgeschlagen", DebugLog.Describe(ex));
        }
        finally
        {
            syncing = false;
            Explorer.IsSyncing = false;
            if (dirty && document.FilePath != null && File.Exists(document.FilePath))
            {
                Save(); // notes written during the sync (or the own version the user chose to keep)
            }
            RefreshExplorer();
        }
    }

    private static bool SamePath(string a, string? b) =>
        b != null && string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// The pull changed (or removed) the open document. With new notes in it the user decides; otherwise it is
    /// reloaded – from the PDF when the .xopp is gone, or closed when nothing is left. False: a message is shown.
    /// </summary>
    private async Task<bool> ResolvePulledDocumentAsync()
    {
        if (dirty)
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Content.XamlRoot,
                Title = "Dokument wurde auf einem anderen Gerät geändert",
                Content = "Die Synchronisierung hat das offene Dokument geändert, während es neue, nicht gespeicherte " +
                    "Notizen hatte.",
                PrimaryButtonText = "Neu laden (meine Notizen verwerfen)",
                SecondaryButtonText = "Meine als Kopie speichern…",
                CloseButtonText = "Meine behalten",
                DefaultButton = ContentDialogButton.Secondary,
            };
            var choice = await ShowDialogAsync(dialog);
            if (choice == ContentDialogResult.None)
            {
                // Saved when the sync ends, over the pulled version; git history keeps that one.
                ShowMessage(InfoBarSeverity.Warning, "Eigene Version behalten",
                    "Die geholte Version wird mit der eigenen überschrieben; sie bleibt in der Git-Historie.");
                return false;
            }
            string? pulledPath = document.FilePath;
            if (choice == ContentDialogResult.Secondary)
            {
                syncing = false; // the git work is done; saving the copy is allowed
                bool saved = await SaveCopyAsync();
                syncing = true;
                if (!saved)
                {
                    ShowMessage(InfoBarSeverity.Warning, "Eigene Version behalten",
                        "Keine Kopie gespeichert: die geholte Version wird mit der eigenen überschrieben; sie bleibt in der Git-Historie.");
                    return false;
                }
            }
            document.FilePath = pulledPath;
        }

        string? path = document.FilePath;
        if (path != null && File.Exists(path))
        {
            await OpenAsync(path);
            return true;
        }
        if (document.PdfPath != null && File.Exists(document.PdfPath))
        {
            await OpenAsync(document.PdfPath);
            return true;
        }
        ShowDocument(XoppDocument.CreateNew(), null);
        ShowMessage(InfoBarSeverity.Warning, "Dokument wurde auf einem anderen Gerät gelöscht oder umbenannt",
            $"{path} gibt es nach der Synchronisierung nicht mehr.");
        return false;
    }

    /// <summary>Saves the open document under a new name without making that the open file.</summary>
    private async Task<bool> SaveCopyAsync()
    {
        string? original = document.FilePath;
        bool saved = await SaveAsAsync();
        if (!saved)
        {
            document.FilePath = original;
        }
        return saved;
    }

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

    // ---------------------------------------------------------------- paste

    private const double Grid = 14.17;

    /// <summary>
    /// Pastes onto the page in the middle of the view, at the left margin below what is already on it (on the next
    /// grid line), so parts of an exercise stack up on a graph page: a copied PDF area or bitmap (e.g. from the
    /// Snipping Tool) as a picture, copied ink as strokes (Windows ink's own clipboard format).
    /// </summary>
    private async void OnPaste(object sender, RoutedEventArgs e)
    {
        var page = document.Pages[CurrentPageIndex()];
        if (!ClipboardService.HasPicture() && page.Ink!.CanPasteFromClipboard())
        {
            PasteInk(page);
            return;
        }
        System.Xml.Linq.XElement? image;
        try
        {
            image = await ClipboardService.PasteImageAsync();
        }
        catch (Exception ex)
        {
            DebugLog.Write("paste", ex);
            ShowMessage(InfoBarSeverity.Warning, "Einfügen nicht möglich", DebugLog.Describe(ex));
            return;
        }
        if (image == null)
        {
            ShowMessage(InfoBarSeverity.Informational, "Nichts zum Einfügen in der Zwischenablage",
                "Einen PDF-Bereich oder Ausschnitt (Ausschneidetool: Win+Umschalt+S) kopieren, oder Striche mit dem Lasso.");
            return;
        }

        var size = XoppImage.GetBounds(image);
        // Wider than the page: scaled down to fit between the margins.
        double scale = Math.Min(1, (page.Width - 2 * Grid) / size.Width);
        var placed = XoppImage.WithBounds(image,
            new Rect(Grid, PasteTop(page, size.Height * scale), size.Width * scale, size.Height * scale));
        page.AddImage(placed);
        var edit = new PageEdit(page);
        edit.AddedImages.Add(placed);
        undo.Push(edit);
    }

    private void PasteInk(XoppPage page)
    {
        var ink = page.Ink!;
        var before = ink.GetStrokes().Select(s => s.Id).ToHashSet();
        ink.PasteFromClipboard(new Point(Grid, PasteTop(page, 0)));
        var edit = new PageEdit(page);
        foreach (var stroke in ink.GetStrokes().Where(s => !before.Contains(s.Id)))
        {
            stroke.Selected = false;
            edit.AddedStrokes.Add(stroke);
        }
        undo.Push(edit);
    }

    private static double PasteTop(XoppPage page, double height)
    {
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        return top < Grid || top + height > page.Height ? Grid : top;
    }
}
