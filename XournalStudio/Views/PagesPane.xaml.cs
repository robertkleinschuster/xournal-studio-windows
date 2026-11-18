using System.Collections.ObjectModel;
using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using XournalStudio.Model;

namespace XournalStudio.Views;

/// <summary>
/// A page in the pages pane: its number, its size as a thumbnail, and the thumbnail in two layers once rendered – the
/// background PDF's page (<see cref="Background"/>) and what lies on it (<see cref="Content"/>: ink, pictures, texts; for
/// a page without PDF its paper too). Kept for its page while the document is shown, so its images stay when pages are
/// inserted, moved or deleted.
/// </summary>
public sealed partial class PageThumb(XoppPage page, double width, double height) : INotifyPropertyChanged
{
    public XoppPage Page { get; } = page;

    private int index;

    public int Index
    {
        get => index;
        set
        {
            index = value;
            Changed(nameof(Number));
        }
    }

    public string Number => (Index + 1).ToString();

    public double Width { get; } = width;

    public double Height { get; } = height;

    /// <summary>The content layer must be drawn again (the page was edited, or it is new).</summary>
    public bool ContentStale { get; set; } = true;

    /// <summary>The background layer was rendered (or failed: not tried again).</summary>
    public bool BackgroundTried { get; set; }

    private ImageSource? background, content;

    public ImageSource? Background
    {
        get => background;
        set
        {
            background = value;
            Changed(nameof(Background));
        }
    }

    public ImageSource? Content
    {
        get => content;
        set
        {
            content = value;
            Changed(nameof(Content));
        }
    }

    private void Changed(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    public event PropertyChangedEventHandler? PropertyChanged;
}

/// <summary>
/// The pages pane (beside the files and the tasks): the document's pages as thumbnails. A tap goes to the page, the
/// selected page follows the page in view; insert (blank or from a PDF) before or after, move up or down (or drag with
/// finger or mouse), delete – all on the selected page (a right-click selects it first). It only asks: the window
/// carries the actions out (<see cref="PagesController"/>); the thumbnails' images come from <see cref="PageThumbnails"/>.
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

    /// <summary>The pages were shown (after <see cref="Show"/>): their thumbnails can be rendered.</summary>
    public event EventHandler? Shown;

    /// <summary>The pages' thumbnails, in the document's order.</summary>
    public IReadOnlyList<PageThumb> Thumbs => items;

    private ObservableCollection<PageThumb> items = [];
    private readonly Dictionary<XoppPage, PageThumb> byPage = new();

    /// <summary>
    /// The document's pages, with the page <paramref name="current"/> gives selected (asked when applied); a page shown
    /// before keeps its thumbnail (not when <paramref name="newDocument"/>). Applied after the running event – never inside
    /// the list's own events: replacing its items while it finishes a reordering drag is not safe in WinUI.
    /// </summary>
    public void Show(IReadOnlyList<XoppPage> pages, Func<int> current, bool newDocument)
    {
        var shown = pages.ToList();
        DispatcherQueue.TryEnqueue(() =>
        {
            if (newDocument)
            {
                byPage.Clear();
            }
            var thumbs = shown.Select((page, i) =>
            {
                if (!byPage.TryGetValue(page, out var thumb))
                {
                    thumb = new PageThumb(page, ThumbWidth, ThumbWidth * page.Height / Math.Max(1, page.Width));
                    byPage[page] = thumb;
                }
                thumb.Index = i;
                return thumb;
            }).ToList();
            foreach (var gone in byPage.Keys.Except(shown).ToList())
            {
                byPage.Remove(gone);
            }
            // A new collection, not Clear/Add (CLAUDE.md: lists are replaced, not mutated); observable, so the list can
            // reorder it while the user drags.
            items = new ObservableCollection<PageThumb>(thumbs);
            List.ItemsSource = items;
            Select(current());
            Shown?.Invoke(this, EventArgs.Empty);
        });
    }

    /// <summary>The page in view changed: it is the selected one.</summary>
    public void Select(int index)
    {
        if (index < 0 || index >= items.Count)
        {
            return;
        }
        List.SelectedIndex = index; // set in code: raises no ItemClick (no navigation)
        List.ScrollIntoView(items[index]);
    }

    // The menu acts on the right-clicked page (kept: the selection may follow the page in view meanwhile); the buttons
    // on the selected page.
    private PageThumb? menuPage;

    private void OnContextRequested(UIElement sender, Microsoft.UI.Xaml.Input.ContextRequestedEventArgs args)
    {
        menuPage = (args.OriginalSource as FrameworkElement)?.DataContext as PageThumb;
        if (menuPage != null)
        {
            List.SelectedIndex = menuPage.Index;
        }
    }

    private void Act(object sender, Action<int> action)
    {
        int target = sender is MenuFlyoutItem && menuPage != null && items.Contains(menuPage)
            ? menuPage.Index
            : Math.Max(0, List.SelectedIndex);
        if (sender is MenuFlyoutItem)
        {
            menuPage = null;
        }
        if (items.Count > 0)
        {
            action(target);
        }
    }

    private void OnItemClick(object sender, ItemClickEventArgs e)
    {
        if (e.ClickedItem is PageThumb thumb)
        {
            GoToRequested?.Invoke(this, thumb.Index);
        }
    }

    private void OnInsertBefore(object sender, RoutedEventArgs e) => Act(sender, i => InsertBlankRequested?.Invoke(this, i));

    private void OnInsertAfter(object sender, RoutedEventArgs e) => Act(sender, i => InsertBlankRequested?.Invoke(this, i + 1));

    private void OnInsertPdf(object sender, RoutedEventArgs e) => Act(sender, i => InsertPdfRequested?.Invoke(this, i));

    private void OnMoveUp(object sender, RoutedEventArgs e) => Act(sender, i =>
    {
        if (i > 0)
        {
            MoveRequested?.Invoke(this, (i, i - 1));
        }
    });

    private void OnMoveDown(object sender, RoutedEventArgs e) => Act(sender, i =>
    {
        if (i < items.Count - 1)
        {
            MoveRequested?.Invoke(this, (i, i + 1));
        }
    });

    private void OnDelete(object sender, RoutedEventArgs e) => Act(sender, i => DeleteRequested?.Invoke(this, i));

    // Reordering by dragging: the list reorders its items itself; the window then moves the page (the pages are shown
    // again after this event).
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
