using System.Collections.ObjectModel;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;
using Launcher = Windows.System.Launcher;
using FolderLauncherOptions = Windows.System.FolderLauncherOptions;
using XournalStudio.Services;

namespace XournalStudio.Views;

public enum ExplorerCommand
{
    NewDocument,
    NewFolder,
    Rename,
    Delete,
}

/// <summary>A command of the explorer's context menu or keyboard for a file or folder (the window carries it out).</summary>
public sealed record ExplorerCommandRequest(ExplorerCommand Command, string Path, bool IsFolder);

/// <summary>
/// The project folder as a TreeView (everything on disk) with git status colors and the sync button.
/// Tapping a document raises <see cref="FileInvoked"/>; the window opens it. The context menu and F2/Del raise
/// <see cref="CommandRequested"/>; showing in File Explorer and copying the path are done here. Changes on disk raise
/// <see cref="FolderChanged"/> (debounced) so the window refreshes the tree.
/// </summary>
public sealed partial class ExplorerView : UserControl
{
    private readonly DispatcherQueueTimer changeTimer;
    private Dictionary<string, ExplorerItem> items = new(StringComparer.OrdinalIgnoreCase);
    private string? folder;
    private int refreshGeneration;
    private FolderWatcher? watcher;
    private string? revealedFile;
    private string? pendingSelection;
    private ExplorerItem? menuItem; // the row the context menu was opened on; null: the project folder

    public ExplorerView()
    {
        InitializeComponent();
        changeTimer = DispatcherQueue.CreateTimer();
        changeTimer.Interval = TimeSpan.FromMilliseconds(400);
        changeTimer.IsRepeating = false;
        changeTimer.Tick += (_, _) =>
        {
            DebugLog.Write("explorer: folder changed on disk");
            FolderChanged?.Invoke(this, EventArgs.Empty);
        };
    }

    public event EventHandler<string>? FileInvoked;

    public event EventHandler? SyncRequested;

    public event EventHandler<ExplorerCommandRequest>? CommandRequested;

    /// <summary>An action done here failed (title, message for the user; already logged).</summary>
    public event EventHandler<(string Title, string Message)>? Failed;

    /// <summary>Files or folders changed on disk (raised once a burst of changes is over).</summary>
    public event EventHandler? FolderChanged;

    /// <summary>Working directory of the folder's git repository (null: not in a repository, no sync).</summary>
    public string? RepositoryRoot { get; private set; }

    public bool IsSyncing
    {
        set
        {
            SyncProgress.IsActive = value;
            SyncButton.Opacity = value ? 0 : 1;
            SyncButton.IsEnabled = !value && RepositoryRoot != null;
        }
    }

    /// <summary>Selects <paramref name="path"/> (e.g. a just created folder) once the next refresh shows it.</summary>
    public void SelectAfterRefresh(string path) => pendingSelection = path;

    /// <summary>
    /// Reads the folder and its git status in the background and shows them; expanded folders stay expanded. The open
    /// document is revealed and selected when it changed; otherwise the selection stays.
    /// </summary>
    public async Task ShowAsync(string? projectFolder, string? openFile)
    {
        int generation = ++refreshGeneration;
        if (!string.Equals(folder, projectFolder, StringComparison.OrdinalIgnoreCase))
        {
            folder = projectFolder;
            Tree.ItemsSource = null;
            items.Clear();
            revealedFile = null;
            Watch(projectFolder);
        }
        FolderName.Text = projectFolder != null ? Path.GetFileName(projectFolder) : "Kein Ordner";
        ToolTipService.SetToolTip(FolderName, projectFolder);
        if (projectFolder == null || !Directory.Exists(projectFolder))
        {
            RepositoryRoot = null;
            SyncButton.IsEnabled = false;
            return;
        }

        var (tree, root, status) = await Task.Run(() =>
        {
            var tree = ProjectTree.Read(projectFolder);
            string? root = null;
            Dictionary<string, GitFileState>? status = null;
            try
            {
                root = GitSync.FindRoot(projectFolder);
                status = root != null ? GitSync.WithFolders(GitSync.GetStatus(root), root) : null;
            }
            catch (Exception e)
            {
                DebugLog.Write("git status", e);
            }
            return (tree, root, status);
        });
        if (generation != refreshGeneration)
        {
            return; // a newer refresh is on its way
        }

        RepositoryRoot = root;
        SyncButton.IsEnabled = root != null && !SyncProgress.IsActive;
        string? selected = (Tree.SelectedItem as ExplorerItem)?.Path;
        bool rebuilt = !SameStructure(tree, items);
        if (rebuilt)
        {
            Rebuild(tree);
        }
        foreach (var item in items.Values)
        {
            item.State = status != null && status.TryGetValue(item.Path, out var s) ? s : GitFileState.Unchanged;
        }

        if (pendingSelection != null && items.ContainsKey(pendingSelection))
        {
            Reveal(pendingSelection);
            pendingSelection = null;
        }
        else if (!string.Equals(openFile, revealedFile, StringComparison.OrdinalIgnoreCase))
        {
            Reveal(openFile);
        }
        else if (rebuilt)
        {
            Reveal(selected != null && items.ContainsKey(selected) ? selected : openFile);
        }
        revealedFile = openFile;
    }

    private void Watch(string? projectFolder)
    {
        watcher?.Dispose();
        watcher = null;
        if (projectFolder == null || !Directory.Exists(projectFolder))
        {
            return;
        }
        try
        {
            watcher = new FolderWatcher(projectFolder);
            // Start restarts the timer: debounced. (A lambda: the method group of a WinRT object fails to marshal.)
            watcher.Changed += (_, _) => DispatcherQueue.TryEnqueue(() => changeTimer.Start());
        }
        catch (Exception e)
        {
            DebugLog.Write($"watch {projectFolder}", e); // the tree still refreshes on activation and saves
        }
    }

    private void Rebuild(ProjectEntry tree)
    {
        var expanded = items.Values.Where(i => i.IsExpanded).Select(i => i.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var newItems = new Dictionary<string, ExplorerItem>(StringComparer.OrdinalIgnoreCase);
        // The selected item is about to go: deselect it first. Selecting a new item while the tree still held one of its
        // old list crashed WinUI when the pane was closed (no containers; 0xC0000005/0xC0000374, 2.6.6-experimental).
        Tree.SelectedItem = null;
        Tree.ItemsSource = tree.Children.Select(entry => Create(entry, null, expanded, newItems)).ToList();
        items = newItems;
        DebugLog.Write($"explorer: tree rebuilt ({items.Count} entries)");
    }

    private static ExplorerItem Create(ProjectEntry entry, ExplorerItem? parent, HashSet<string> expanded,
        Dictionary<string, ExplorerItem> all)
    {
        var item = new ExplorerItem(entry) { Parent = parent, IsExpanded = expanded.Contains(entry.Path) };
        all[entry.Path] = item;
        foreach (var child in entry.Children)
        {
            item.Children.Add(Create(child, item, expanded, all));
        }
        return item;
    }

    private static bool SameStructure(ProjectEntry tree, Dictionary<string, ExplorerItem> items)
    {
        int count = 0;
        bool Visit(ProjectEntry entry)
        {
            foreach (var child in entry.Children)
            {
                count++;
                if (!items.ContainsKey(child.Path) || !Visit(child))
                {
                    return false;
                }
            }
            return true;
        }
        return Visit(tree) && count == items.Count;
    }

    private void Reveal(string? file)
    {
        if (file == null || !items.TryGetValue(file, out var item))
        {
            return;
        }
        for (var parent = item.Parent; parent != null; parent = parent.Parent)
        {
            parent.IsExpanded = true;
        }
        Tree.UpdateLayout(); // the containers of just expanded folders must exist before selecting
        Tree.SelectedItem = item;
    }

    private void OnItemInvoked(TreeView sender, TreeViewItemInvokedEventArgs args)
    {
        if (args.InvokedItem is not ExplorerItem item)
        {
            return;
        }
        if (item.IsFolder)
        {
            item.IsExpanded = !item.IsExpanded;
        }
        else
        {
            FileInvoked?.Invoke(this, item.Path);
        }
    }

    private void OnSync(object sender, RoutedEventArgs e) => SyncRequested?.Invoke(this, EventArgs.Empty);

    // ---------------------------------------------------------------- context menu and keys

    /// <summary>The rows share one menu: it applies to the row it opens on, which gets selected (as in File Explorer).</summary>
    private void OnItemMenuOpening(object? sender, object e)
    {
        var menu = (MenuFlyout)sender!;
        menuItem = menu.Target is TreeViewItem container ? Tree.ItemFromContainer(container) as ExplorerItem : null;
        if (menuItem != null)
        {
            Tree.SelectedItem = menuItem;
        }
        foreach (var entry in menu.Items.Where(i => (string?)i.Tag == "file"))
        {
            entry.Visibility = menuItem is { IsFolder: false } ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    /// <summary>The tree's empty area stands for the project folder.</summary>
    private void OnFolderMenuOpening(object? sender, object e)
    {
        menuItem = null;
        foreach (var entry in ((MenuFlyout)sender!).Items)
        {
            entry.IsEnabled = folder != null;
        }
    }

    private (string Path, bool IsFolder)? MenuTarget =>
        menuItem != null ? (menuItem.Path, menuItem.IsFolder) : folder != null ? (folder, true) : null;

    private void Request(ExplorerCommand command, (string Path, bool IsFolder)? target)
    {
        if (target is { } t)
        {
            CommandRequested?.Invoke(this, new ExplorerCommandRequest(command, t.Path, t.IsFolder));
        }
    }

    private void OnOpenItem(object sender, RoutedEventArgs e)
    {
        if (menuItem is { IsFolder: false } item)
        {
            FileInvoked?.Invoke(this, item.Path);
        }
    }

    private void OnNewDocument(object sender, RoutedEventArgs e) => Request(ExplorerCommand.NewDocument, MenuTarget);

    private void OnNewFolder(object sender, RoutedEventArgs e) => Request(ExplorerCommand.NewFolder, MenuTarget);

    private void OnRename(object sender, RoutedEventArgs e) => Request(ExplorerCommand.Rename, MenuTarget);

    private void OnDelete(object sender, RoutedEventArgs e) => Request(ExplorerCommand.Delete, MenuTarget);

    private void OnShowInExplorer(object sender, RoutedEventArgs e) => _ = ShowInExplorerAsync(MenuTarget);

    private void OnCopyPath(object sender, RoutedEventArgs e) => CopyPath(MenuTarget);

    private (string Path, bool IsFolder)? SelectedTarget =>
        Tree.SelectedItem is ExplorerItem item ? (item.Path, item.IsFolder) : null;

    private void OnRenameKey(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = SelectedTarget != null;
        Request(ExplorerCommand.Rename, SelectedTarget);
    }

    private void OnDeleteKey(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = SelectedTarget != null;
        Request(ExplorerCommand.Delete, SelectedTarget);
    }

    private void OnCopyPathKey(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        args.Handled = SelectedTarget != null;
        CopyPath(SelectedTarget);
    }

    /// <summary>Opens the containing folder in File Explorer with the item selected (Windows' launcher).</summary>
    private async Task ShowInExplorerAsync((string Path, bool IsFolder)? target)
    {
        if (target is not { } t)
        {
            return;
        }
        try
        {
            string? parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(t.Path));
            if (parent == null)
            {
                await Launcher.LaunchFolderPathAsync(t.Path);
                return;
            }
            IStorageItem item = t.IsFolder
                ? await StorageFolder.GetFolderFromPathAsync(t.Path)
                : await StorageFile.GetFileFromPathAsync(t.Path);
            var options = new FolderLauncherOptions();
            options.ItemsToSelect.Add(item);
            if (!await Launcher.LaunchFolderPathAsync(parent, options))
            {
                DebugLog.Write($"show in explorer {t.Path}: the launcher returned false");
            }
        }
        catch (Exception e)
        {
            DebugLog.Write($"show in explorer {t.Path}", e);
            Failed?.Invoke(this, ("Im Explorer anzeigen nicht möglich", DebugLog.Describe(e)));
        }
    }

    /// <summary>Copies the full path in quotes, as File Explorer's "Als Pfad kopieren" (Ctrl+Shift+C) does.</summary>
    private void CopyPath((string Path, bool IsFolder)? target)
    {
        if (target is not { } t)
        {
            return;
        }
        var package = new DataPackage();
        package.SetText($"\"{t.Path}\"");
        try
        {
            Clipboard.SetContent(package); // not flushed: see CLAUDE.md (clipboard history holds the clipboard)
        }
        catch (Exception e)
        {
            DebugLog.Write("copy path", e);
            Failed?.Invoke(this, ("Pfad nicht kopiert", DebugLog.Describe(e)));
        }
    }
}
