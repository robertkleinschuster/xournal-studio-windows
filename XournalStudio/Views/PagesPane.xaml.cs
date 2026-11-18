using System.Collections.ObjectModel;
using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace XournalStudio.Views;

/// <summary>A page in the pages pane: its number, its size as a thumbnail and the thumbnail once rendered.</summary>
public sealed partial class PageThumb(int index, double width, double height) : INotifyPropertyChanged
{
    public int Index { get; } = index;

    public string Number => (Index + 1).ToString();

    public double Width { get; } = width;

    public double Height { get; } = height;

    private ImageSource? image;

    public ImageSource? Image
    {
        get => image;
        set
        {
            image = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Image)));
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
}

/// <summary>
/// The pages pane (beside the files and the tasks): the document's pages as thumbnails. A tap goes to the page, the
/// selected page follows the page in view; insert (blank or from a PDF) before or after, move up or down (or drag with
/// finger or mouse), delete. It only asks: the window carries the actions out (<see cref="PagesController"/>).
/// </summary>
public sealed partial class PagesPane : UserControl
{
    /// <summary>Thumbnail width in DIPs (the pane is 300 wide).</summary>
    public const double ThumbWidth = 180;

    public PagesPane()
    {
        InitializeComponent();
    }

    public event EventHandler<int>? GoToRequested;

    /// <summary>A blank page at this index.</summary>
    public event EventHandler<int>? InsertBlankRequested;

    /// <summary>Pages of a PDF around this page (the dialog asks before or after).</summary>
    public event EventHandler<int>? InsertPdfRequested;

    /// <summary>Move a page: (from, to).</summary>
    public event EventHandler<(int From, int To)>? MoveRequested;

    public event EventHandler<int>? DeleteRequested;

    private ObservableCollection<PageThumb> items = [];
    private bool showing; // selection set by the app, not the user

    /// <summary>The document's pages (their sizes in points), with <paramref name="current"/> selected.</summary>
    public void Show(IReadOnlyList<(double Width, double Height)> sizes, int current)
    {
        // A new collection each time, not Clear/Add (CLAUDE.md: lists are replaced, not mutated); observable, so the list
        // can reorder it while the user drags.
        items = new(sizes.Select((s, i) => new PageThumb(i, ThumbWidth, ThumbWidth * s.Height / Math.Max(1, s.Width))));
        showing = true;
        List.ItemsSource = items;
        Select(current);
        showing = false;
    }

    /// <summary>The page in view changed: it is the selected one.</summary>
    public void Select(int index)
    {
        if (index < 0 || index >= items.Count)
        {
            return;
        }
        showing = true;
        List.SelectedIndex = index;
        List.ScrollIntoView(items[index]);
        showing = false;
    }

    public int Count => items.Count;

    public void SetThumbnail(int index, ImageSource? image)
    {
        if (index >= 0 && index < items.Count)
        {
            items[index].Image = image;
        }
    }

    // The page an action applies to: the one right-clicked, else the selected one.
    private int Target => menuTarget ?? Math.Max(0, List.SelectedIndex);

    private int? menuTarget;

    private void OnContextRequested(UIElement sender, Microsoft.UI.Xaml.Input.ContextRequestedEventArgs args)
    {
        menuTarget = (args.OriginalSource as FrameworkElement)?.DataContext is PageThumb thumb ? thumb.Index : null;
        if (menuTarget is { } index)
        {
            Select(index);
        }
    }

    private void Act(Action<int> action)
    {
        int target = Target;
        menuTarget = null;
        if (items.Count > 0)
        {
            action(target);
        }
    }

    private void OnItemClick(object sender, ItemClickEventArgs e)
    {
        if (!showing && e.ClickedItem is PageThumb thumb)
        {
            GoToRequested?.Invoke(this, thumb.Index);
        }
    }

    private void OnInsertBefore(object sender, RoutedEventArgs e) => Act(i => InsertBlankRequested?.Invoke(this, i));

    private void OnInsertAfter(object sender, RoutedEventArgs e) => Act(i => InsertBlankRequested?.Invoke(this, i + 1));

    private void OnInsertPdf(object sender, RoutedEventArgs e) => Act(i => InsertPdfRequested?.Invoke(this, i));

    private void OnMoveUp(object sender, RoutedEventArgs e) => Act(i =>
    {
        if (i > 0)
        {
            MoveRequested?.Invoke(this, (i, i - 1));
        }
    });

    private void OnMoveDown(object sender, RoutedEventArgs e) => Act(i =>
    {
        if (i < items.Count - 1)
        {
            MoveRequested?.Invoke(this, (i, i + 1));
        }
    });

    private void OnDelete(object sender, RoutedEventArgs e) => Act(i => DeleteRequested?.Invoke(this, i));

    // Reordering by dragging: the list reorders its items itself; the window then moves the page and shows the pages
    // again (the numbers follow).
    private PageThumb? dragged;

    private void OnDragItemsStarting(object sender, DragItemsStartingEventArgs e) => dragged = e.Items.FirstOrDefault() as PageThumb;

    private void OnDragItemsCompleted(ListViewBase sender, DragItemsCompletedEventArgs args)
    {
        var thumb = dragged;
        dragged = null;
        if (thumb == null)
        {
            return;
        }
        int to = items.IndexOf(thumb);
        if (to >= 0 && to != thumb.Index)
        {
            MoveRequested?.Invoke(this, (thumb.Index, to));
        }
    }
}
