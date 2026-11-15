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
                        ShowMessage(InfoBarSeverity.Warning, "PDF-Hintergrund nicht gefunden", $"{doc.PdfPath}: {e.Message}");
                    }
                }
                ShowDocument(doc, pdfBackground);
            }
            settings.LastFile = path;
            settings.Save();
        }
        catch (Exception e)
        {
            ShowMessage(InfoBarSeverity.Error, "Datei konnte nicht geöffnet werden", $"{path}: {e.Message}");
        }
    }

    private void ShowDocument(XoppDocument doc, PdfBackground? pdfBackground)
    {
        document = doc;
        pdf = pdfBackground;
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
            ShowMessage(InfoBarSeverity.Error, "Speichern fehlgeschlagen", $"{document.FilePath}: {e.Message}");
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

    private void OnUndo(object sender, RoutedEventArgs e) => undo.Undo();

    private void OnRedo(object sender, RoutedEventArgs e) => undo.Redo();

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
                DebugLog.Write($"git root not found: {e.Message}");
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
            ShowMessage(InfoBarSeverity.Error, "Konflikt beim Synchronisieren",
                $"{ex.Message}. Die eigenen Änderungen sind committet, aber nicht hochgeladen; den Konflikt mit Git lösen.");
        }
        catch (Exception ex)
        {
            ShowMessage(InfoBarSeverity.Error, "Synchronisieren fehlgeschlagen", ex.Message);
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

    /// <summary>
    /// Pastes a bitmap from the clipboard onto the page in the middle of the view: at the left margin, below what is
    /// already on it (on the next grid line), so parts of an exercise stack up on a graph page.
    /// </summary>
    private async void OnPaste(object sender, RoutedEventArgs e)
    {
        System.Xml.Linq.XElement? image;
        try
        {
            image = await ClipboardService.PasteImageAsync();
        }
        catch (Exception ex)
        {
            ShowMessage(InfoBarSeverity.Warning, "Einfügen nicht möglich", ex.Message);
            return;
        }
        if (image == null)
        {
            ShowMessage(InfoBarSeverity.Informational, "Kein Bild in der Zwischenablage",
                "Einen Ausschnitt z. B. mit dem Ausschneidetool (Win+Umschalt+S) kopieren.");
            return;
        }

        var page = document.Pages[CurrentPageIndex()];
        var size = XoppImage.GetBounds(image);
        const double Grid = 14.17;
        // Wider than the page: scaled down to fit between the margins.
        double scale = Math.Min(1, (page.Width - 2 * Grid) / size.Width);
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        if (top < Grid || top + size.Height * scale > page.Height)
        {
            top = Grid;
        }
        var placed = XoppImage.WithBounds(image, new Rect(Grid, top, size.Width * scale, size.Height * scale));
        page.AddImage(placed);
        var edit = new PageEdit(page);
        edit.AddedImages.Add(placed);
        undo.Push(edit);
    }
}
