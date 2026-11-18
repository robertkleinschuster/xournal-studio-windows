using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Seiten aus PDF einfügen (page menu and the pages pane): pages of another PDF (e.g. a cover sheet) before or after a
/// page, chosen as thumbnails, stored as upstream Xournal++ stores a document – one background PDF, pages refer to its
/// pages by number. The chosen pages are appended to the end of the document's PDF (<see cref="PdfPageAppender"/>, the
/// file changes at once; the dialog says so); a page of the document's own PDF is just referred to, and a document
/// without PDF takes the chosen PDF as its PDF.
/// </summary>
public sealed class InsertPdfPagesCommand(WindowId window, DocumentSession session, PagesController pages, Dialogs dialogs, Messages messages)
{
    private const uint ThumbnailWidth = 120; // pixels: a page about as wide as a stamp, its layout recognizable

    /// <summary>Inserts pages before or after page <paramref name="around"/> (0-based; the dialog asks which).</summary>
    public async Task InsertAsync(int around)
    {
        var picker = new FileOpenPicker(window);
        picker.FileTypeFilter.Add(".pdf");
        string? source = (await picker.PickSingleFileAsync())?.Path;
        if (source == null)
        {
            return;
        }
        var doc = session.Document;
        string? target = doc.PdfPath;
        bool same = target != null && ProjectFiles.SamePath(source, target);
        PdfBackground? sourcePdf = null;
        bool kept = false; // the source became the document's PDF (then it is never disposed here)
        try
        {
            sourcePdf = await PdfBackground.LoadAsync(source);
            string where = target == null
                ? $"Das Dokument verwendet dann „{Path.GetFileName(source)}“ als PDF-Hintergrund."
                : same
                    ? "Es ist das PDF des Dokuments: Die Seiten werden nur eingefügt."
                    : $"Die Seiten werden ans Ende von „{Path.GetFileName(target)}“ angehängt – das PDF des Dokuments wird " +
                      "dabei sofort geändert (wie in Xournal++ hat ein Dokument ein PDF), seine bisherigen Seiten bleiben.";
            if (await AskAsync(Path.GetFileName(source), sourcePdf, where, around) is not { } choice)
            {
                return;
            }
            int at = choice.Before ? around : around + 1;
            if (target == null || same)
            {
                session.UsePdf(source, sourcePdf);
                kept = true;
                pages.InsertPdfPagesAt(at, choice.Pages, sourcePdf);
            }
            else
            {
                int first = await Task.Run(() => PdfPageAppender.Append(target, source, choice.Pages));
                var background = await PdfBackground.LoadAsync(target);
                session.UsePdf(target, background);
                pages.InsertPdfPagesAt(at, Enumerable.Range(first, choice.Pages.Count).ToList(), background);
            }
            messages.Show(InfoBarSeverity.Success, "Seiten eingefügt",
                $"{choice.Pages.Count} Seite(n) aus {Path.GetFileName(source)}" + (target == null || same ? "." : $" – an {Path.GetFileName(target)} angehängt."));
        }
        catch (Exception e)
        {
            messages.Error("insert PDF pages", "Seiten konnten nicht eingefügt werden", e, source);
        }
        finally
        {
            if (!kept)
            {
                sourcePdf?.Dispose(); // only read for the dialog (and the append): its copy in memory goes now
            }
        }
    }

    // The pages to insert (thumbnails to tap, in the PDF's order) and before or after which page; null: canceled.
    private async Task<(List<int> Pages, bool Before)?> AskAsync(string file, PdfBackground pdf, string where, int around)
    {
        int count = pdf.PageCount;
        var thumbnails = new GridView
        {
            SelectionMode = ListViewSelectionMode.Multiple,
            MaxHeight = 360,
            ItemsSource = Enumerable.Range(0, count).Select(Thumbnail).ToList(),
        };
        var all = new HyperlinkButton { Content = "Alle auswählen", Padding = new Thickness(0) };
        all.Click += (_, _) => thumbnails.SelectAll();
        var after = new RadioButton { Content = $"nach Seite {around + 1}", IsChecked = true, GroupName = "where" };
        var before = new RadioButton { Content = $"vor Seite {around + 1}", GroupName = "where" };
        var dialog = new ContentDialog
        {
            Title = $"Seiten aus „{file}“ einfügen",
            Content = new StackPanel
            {
                Spacing = 8,
                Children =
                {
                    Dialogs.Text($"{count} {(count == 1 ? "Seite" : "Seiten")} – die gewünschten antippen."),
                    all,
                    thumbnails,
                    new StackPanel { Orientation = Orientation.Horizontal, Spacing = 16, Children = { after, before } },
                    Dialogs.Text(where),
                },
            },
            PrimaryButtonText = "Einfügen",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
            IsPrimaryButtonEnabled = count == 1,
        };
        if (count == 1)
        {
            thumbnails.SelectAll(); // one page: nothing to choose
        }
        thumbnails.SelectionChanged += (_, _) => dialog.IsPrimaryButtonEnabled = thumbnails.SelectedItems.Count > 0;
        using var closed = new CancellationTokenSource();
        var items = (List<FrameworkElement>)thumbnails.ItemsSource;
        var rendering = RenderThumbnailsAsync(pdf, items, closed.Token);
        ContentDialogResult result;
        try
        {
            result = await dialogs.ShowAsync(dialog);
        }
        finally // also when the dialog could not be shown: no rendering goes on, no thumbnail is kept
        {
            closed.Cancel(); // the rest of the thumbnails is not needed any more (also renders still waiting)
            await rendering;
            foreach (var item in items)
            {
                var image = (Image)((Border)((StackPanel)item).Children[0]).Child;
                Services.PictureRenderer.Release(image.Source);
                image.Source = null;
            }
        }
        if (result != ContentDialogResult.Primary)
        {
            return null;
        }
        var chosen = thumbnails.SelectedItems.Cast<FrameworkElement>().Select(t => (int)t.Tag).Order().ToList();
        return (chosen, before.IsChecked == true);
    }

    private static FrameworkElement Thumbnail(int index) => new StackPanel
    {
        Tag = index,
        Width = ThumbnailWidth / 1.5 + 8,
        Spacing = 2,
        Padding = new Thickness(4),
        Children =
        {
            new Border
            {
                Height = ThumbnailWidth / 1.5 * 1.414, // A4's proportion until the page is rendered
                Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Colors.White),
                BorderBrush = (Microsoft.UI.Xaml.Media.Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
                BorderThickness = new Thickness(1),
                Child = new Image { Stretch = Microsoft.UI.Xaml.Media.Stretch.Uniform },
            },
            new TextBlock { Text = (index + 1).ToString(), HorizontalAlignment = HorizontalAlignment.Center },
        },
    };

    // One page after the other (Windows.Data.Pdf renders one page at a time), until the dialog closes.
    private static async Task RenderThumbnailsAsync(PdfBackground pdf, List<FrameworkElement> items, CancellationToken closed)
    {
        foreach (var item in items)
        {
            if (closed.IsCancellationRequested)
            {
                return;
            }
            try
            {
                var frame = (Border)((StackPanel)item).Children[0];
                if (await pdf.RenderAsync((int)item.Tag, ThumbnailWidth, closed) is { } image)
                {
                    frame.Height = double.NaN;
                    ((Image)frame.Child).Source = image;
                }
            }
            catch (OperationCanceledException)
            {
                return; // the dialog closed
            }
            catch (Exception e)
            {
                DebugLog.Write($"insert PDF pages: thumbnail of page {(int)item.Tag + 1}", e); // that page shows no thumbnail
            }
        }
    }
}
