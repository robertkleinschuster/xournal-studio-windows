using System.Collections.ObjectModel;
using System.ComponentModel;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>A row of the explorer's TreeView: folder or document, with its git state (new = green, modified = blue).</summary>
public sealed partial class ExplorerItem(ProjectEntry entry) : INotifyPropertyChanged
{
    private GitFileState state;
    private bool isExpanded;

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Path { get; } = entry.Path;

    public string Name { get; } = entry.Name;

    public bool IsFolder { get; } = entry.IsFolder;

    public ObservableCollection<ExplorerItem> Children { get; } = [];

    public ExplorerItem? Parent { get; init; }

    public string Glyph => IsFolder ? "" : Path.EndsWith(".pdf", StringComparison.OrdinalIgnoreCase) ? "" : "";

    public bool IsExpanded
    {
        get => isExpanded;
        set
        {
            if (isExpanded != value)
            {
                isExpanded = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsExpanded)));
            }
        }
    }

    public GitFileState State
    {
        get => state;
        set
        {
            if (state != value)
            {
                state = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsNew)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsModified)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsUnchanged)));
            }
        }
    }

    // The template shows the name in the theme color of the state (theme resources follow light/dark changes).
    public bool IsNew => State == GitFileState.New;

    public bool IsModified => State == GitFileState.Modified;

    public bool IsUnchanged => State == GitFileState.Unchanged;
}
