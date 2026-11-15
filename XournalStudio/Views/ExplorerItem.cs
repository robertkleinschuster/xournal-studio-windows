using System.Collections.ObjectModel;
using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>A row of the explorer's TreeView: folder or document, with its git state as text color.</summary>
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
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Foreground)));
            }
        }
    }

    /// <summary>New = green, modified = blue (accent), otherwise the normal text color.</summary>
    public Brush Foreground => (Brush)Application.Current.Resources[State switch
    {
        GitFileState.New => "SystemFillColorSuccessBrush",
        GitFileState.Modified => "AccentTextFillColorPrimaryBrush",
        _ => "TextFillColorPrimaryBrush",
    }];
}
