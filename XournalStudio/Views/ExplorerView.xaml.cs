using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The project folder as a TreeView (folders, .xopp, .pdf) with git status colors and the sync button.
/// Tapping a document raises <see cref="FileInvoked"/>; the window opens it.
/// </summary>
public sealed partial class ExplorerView : UserControl
{
    private readonly ObservableCollection<ExplorerItem> roots = [];
    private Dictionary<string, ExplorerItem> items = new(StringComparer.OrdinalIgnoreCase);
    private string? folder;
    private int refreshGeneration;

    public ExplorerView()
    {
        InitializeComponent();
        Tree.ItemsSource = roots;
    }

    public event EventHandler<string>? FileInvoked;

    public event EventHandler? SyncRequested;

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

    /// <summary>
    /// Reads the folder and its git status in the background and shows them; expanded folders stay expanded and the
    /// open document is revealed and selected.
    /// </summary>
    public async Task ShowAsync(string? projectFolder, string? openFile)
    {
        int generation = ++refreshGeneration;
        if (!string.Equals(folder, projectFolder, StringComparison.OrdinalIgnoreCase))
        {
            folder = projectFolder;
            roots.Clear();
            items.Clear();
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
                status = root != null ? GitSync.GetStatus(root) : null;
            }
            catch (Exception e)
            {
                DebugLog.Write($"git status failed: {e.Message}");
            }
            return (tree, root, status);
        });
        if (generation != refreshGeneration)
        {
            return; // a newer refresh is on its way
        }

        RepositoryRoot = root;
        SyncButton.IsEnabled = root != null && !SyncProgress.IsActive;
        if (!SameStructure(tree, items))
        {
            Rebuild(tree);
        }
        foreach (var item in items.Values)
        {
            item.State = status != null && !item.IsFolder && status.TryGetValue(item.Path, out var s) ? s : GitFileState.Unchanged;
        }
        Reveal(openFile);
    }

    private void Rebuild(ProjectEntry tree)
    {
        var expanded = items.Values.Where(i => i.IsExpanded).Select(i => i.Path).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var newItems = new Dictionary<string, ExplorerItem>(StringComparer.OrdinalIgnoreCase);
        roots.Clear();
        foreach (var entry in tree.Children)
        {
            roots.Add(Create(entry, null, expanded, newItems));
        }
        items = newItems;
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
        // A PDF without annotations yet is shown under its own name; with them, its .xopp is.
        if (file == null || !(items.TryGetValue(file, out var item) ||
            items.TryGetValue(Path.ChangeExtension(file, ".pdf"), out item)))
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
}
