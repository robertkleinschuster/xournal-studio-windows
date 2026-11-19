using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Seiten einfügen (page menu and the pages pane): pages of another PDF (e.g. a cover sheet) or of another document
/// (.xopp) before or after a page, chosen as thumbnails, stored as upstream Xournal++ stores a document – one background
/// PDF, pages refer to its pages by number (<see cref="BringPdfPagesAsync"/>: PDF pages that are not the document's own are
/// appended to the end of its PDF, the file changes at once and the dialog says so; a document without PDF takes the
/// source's PDF as its PDF). Pages of a document are copied with all that lies on them (<see cref="XoppDocument.CopyPages"/>).
/// Thumbnails are rendered page by page while the dialog is open: a PDF page, and for a document's page its PDF page with
/// what lies on it drawn over it (<see cref="PageThumbnails.ContentAsync"/>).
/// </summary>
public sealed class InsertPagesCommand(WindowId window, DocumentSession session, PagesController pages, Dialogs dialogs, Messages messages)
{
    private const uint ThumbnailWidth = 120; // pixels: a page about as wide as a stamp, its layout recognizable

    /// <summary>Inserts pages before or after page <paramref name="around"/> (0-based; the dialog asks which).</summary>
    public async Task InsertAsync(int around)
    {
        string? source = await Pickers.OpenAsync(window, Pickers.Pdf, Pickers.Document);
        if (source == null)
        {
            return;
        }
        PdfBackground? sourcePdf = null;
        bool kept = false; // the source's PDF became the document's (then it is never disposed here)
        try
        {
            var doc = session.Document;
            if (Path.GetExtension(source).Equals(Pickers.Document, StringComparison.OrdinalIgnoreCase))
            {
                var from = await Task.Run(() => XoppDocument.Load(source));
                string? pdfPath = from.PdfPath != null && File.Exists(from.PdfPath) ? from.PdfPath : null;
                sourcePdf = pdfPath != null ? await PdfBackground.LoadAsync(pdfPath) : null;
                kept = await InsertFromDocumentAsync(doc, source, from, sourcePdf, around);
            }
            else
            {
                sourcePdf = await PdfBackground.LoadAsync(source);
                kept = await InsertFromPdfAsync(doc, source, sourcePdf, around);
            }
        }
        catch (Exception e)
        {
            messages.Error("insert pages", "Seiten konnten nicht eingefügt werden", e, source);
        }
        finally
        {
            if (!kept)
            {
                sourcePdf?.Dispose(); // only read for the dialog (and the append): its copy in memory goes now
            }
        }
    }

    // Pages of a PDF. True: the PDF became the document's.
    private async Task<bool> InsertFromPdfAsync(XoppDocument doc, string source, PdfBackground pdf, int around)
    {
        string where = WhereText(doc, source, null, "Die Seiten werden");
        var choice = await AskAsync(Path.GetFileName(source), pdf.PageCount,
            async (i, closed) => (await pdf.RenderAsync(i, ThumbnailWidth, closed), null), where, around);
        if (choice == null || await BringPdfPagesAsync(doc, source, pdf, choice.Value.Pages) is not { } brought)
        {
            return false;
        }
        pages.InsertPdfPagesAt(choice.Value.Before ? around : around + 1, choice.Value.Pages.Select(brought.PageOf).ToList(),
            brought.Background ?? session.Pdf!);
        Done(choice.Value.Pages.Count, source, doc, brought.Appended);
        return brought.Adopted;
    }

    // Pages of another document, copied with everything on them. True: its PDF became the document's.
    private async Task<bool> InsertFromDocumentAsync(XoppDocument doc, string source, XoppDocument from, PdfBackground? fromPdf, int around)
    {
        if (from.Pages.Count == 0)
        {
            messages.Show(InfoBarSeverity.Warning, "Keine Seiten", $"{Path.GetFileName(source)} hat keine Seiten.");
            return false;
        }
        string where = from.Pages.Any(p => p.IsPdf) && from.PdfPath != null
            ? WhereText(doc, from.PdfPath, source, "Die Seiten werden mit allem, was darauf liegt, kopiert; ihre PDF-Seiten werden")
            : "Die Seiten werden mit allem, was darauf liegt, kopiert.";
        var choice = await AskAsync(Path.GetFileName(source), from.Pages.Count, async (i, closed) =>
        {
            var page = from.Pages[i];
            var background = page.IsPdf && fromPdf != null ? await fromPdf.RenderAsync(page.PdfPageIndex, ThumbnailWidth, closed) : null;
            return (background, await PageThumbnails.ContentAsync(page, ThumbnailWidth, closed));
        }, where, around);
        if (choice == null)
        {
            return false;
        }
        var pdfPages = from.PdfPagesOf(choice.Value.Pages);
        (Func<int, int> PageOf, PdfBackground? Background, bool Appended, bool Adopted) brought = (p => p, null, false, false);
        if (pdfPages.Count > 0)
        {
            if (fromPdf == null)
            {
                // A document whose PDF is missing (or not named): its PDF pages cannot be brought along.
                messages.Show(InfoBarSeverity.Warning, "Seiten nicht eingefügt",
                    $"Das PDF der Seiten fehlt: {from.PdfPath ?? $"{Path.GetFileName(source)} nennt keines"}");
                return false;
            }
            if (await BringPdfPagesAsync(doc, from.PdfPath!, fromPdf, pdfPages) is not { } pdf)
            {
                return false;
            }
            brought = pdf;
        }
        pages.InsertCopiedPagesAt(choice.Value.Before ? around : around + 1,
            XoppDocument.CopyPages(from, choice.Value.Pages, brought.PageOf), brought.Background);
        Done(choice.Value.Pages.Count, source, doc, brought.Appended);
        return brought.Adopted;
    }

    /// <summary>
    /// The document gets <paramref name="sourcePages"/> of <paramref name="sourcePdf"/> as pages of its one PDF: the same PDF
    /// is just referred to; a document without PDF takes the source as its PDF (Adopted); otherwise they are appended to
    /// its PDF (written at once; not during a sync). PageOf maps a source page to the document's PDF page; Background is the
    /// document's PDF when it changed. Null: refused, or the document was left meanwhile (said).
    /// </summary>
    private async Task<(Func<int, int> PageOf, PdfBackground? Background, bool Appended, bool Adopted)?> BringPdfPagesAsync(
        XoppDocument doc, string sourcePdf, PdfBackground loaded, IReadOnlyList<int> sourcePages)
    {
        if (!StillOpen(doc))
        {
            return null;
        }
        string? target = doc.PdfPath;
        if (target != null && ProjectFiles.SamePath(target, sourcePdf))
        {
            return (p => p, null, false, false);
        }
        if (target == null)
        {
            session.UsePdf(sourcePdf, loaded);
            return (p => p, loaded, false, true);
        }
        if (session.RefusedWhileSyncing("Seiten an das PDF anhängen"))
        {
            return null; // appending writes the document's PDF, which git may be working on
        }
        var list = sourcePages.ToList();
        int first = await Task.Run(() => PdfPageAppender.Append(target, sourcePdf, list));
        var background = await PdfBackground.LoadAsync(target);
        if (!StillOpen(doc))
        {
            background.Dispose();
            return null; // the pages are in the PDF's end now (unused, harmless); nothing is inserted elsewhere
        }
        session.UsePdf(target, background);
        return (p => first + list.IndexOf(p), background, true, false);
    }

    // The insert goes on only into the document it began in.
    private bool StillOpen(XoppDocument doc)
    {
        if (session.Document == doc)
        {
            return true;
        }
        messages.Show(InfoBarSeverity.Informational, "Seiten nicht eingefügt", "Inzwischen ist ein anderes Dokument geöffnet.");
        return false;
    }

    // What happens to the document's PDF, for the dialog. sourceDocument: the .xopp whose PDF it is (null: a PDF).
    private static string WhereText(XoppDocument doc, string sourcePdf, string? sourceDocument, string start)
    {
        string? target = doc.PdfPath;
        if (target != null && ProjectFiles.SamePath(target, sourcePdf))
        {
            return sourceDocument == null ? "Es ist das PDF des Dokuments: Die Seiten werden nur eingefügt." : start.Split(';')[0] + ".";
        }
        if (target == null)
        {
            // An attached PDF ("<doc>.xopp.bg.pdf") belongs to the other document's name: renamed or deleted with it.
            string attached = sourceDocument != null && Path.GetFileName(sourcePdf).StartsWith(Path.GetFileName(sourceDocument) + ".",
                StringComparison.OrdinalIgnoreCase)
                ? $" Es ist ein Anhang von „{Path.GetFileName(sourceDocument)}“: Wird das umbenannt oder gelöscht, fehlt der Hintergrund."
                : "";
            return (sourceDocument == null ? "" : start.Split(';')[0] + ". ") +
                $"Das Dokument verwendet dann „{Path.GetFileName(sourcePdf)}“ als PDF-Hintergrund." + attached;
        }
        return $"{start} ans Ende von „{Path.GetFileName(target)}“ angehängt – das PDF des Dokuments wird dabei sofort geändert " +
            "(wie in Xournal++ hat ein Dokument ein PDF), seine bisherigen Seiten bleiben.";
    }

    private void Done(int count, string source, XoppDocument doc, bool appended) =>
        messages.Show(InfoBarSeverity.Success, "Seiten eingefügt", $"{count} Seite(n) aus {Path.GetFileName(source)}" +
            (appended ? $" – PDF-Seiten an {Path.GetFileName(doc.PdfPath)} angehängt." : "."));

    // The pages to insert (thumbnails to tap, in the source's order) and before or after which page; null: canceled.
    // render gives a page's thumbnail: its background (PDF page) and what lies on it (null: nothing).
    private async Task<(List<int> Pages, bool Before)?> AskAsync(string file, int count,
        Func<int, CancellationToken, Task<(ImageSource? Background, ImageSource? Content)>> render, string where, int around)
    {
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
        var rendering = RenderThumbnailsAsync(render, items, closed.Token);
        ContentDialogResult result;
        try
        {
            result = await dialogs.ShowAsync(dialog);
        }
        finally // also when the dialog could not be shown: no rendering goes on, no thumbnail is kept
        {
            closed.Cancel(); // the rest of the thumbnails is not needed any more (also renders still waiting)
            await rendering;
            foreach (var image in items.SelectMany(Layers))
            {
                PictureRenderer.Release(image.Source);
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

    // A thumbnail: a white sheet with two layers (background, content over it) and the page number.
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
                Background = new SolidColorBrush(Colors.White),
                BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
                BorderThickness = new Thickness(1),
                Child = new Grid
                {
                    Children =
                    {
                        new Image { Stretch = Stretch.Uniform },
                        new Image { Stretch = Stretch.Uniform },
                    },
                },
            },
            new TextBlock { Text = (index + 1).ToString(), HorizontalAlignment = HorizontalAlignment.Center },
        },
    };

    private static IEnumerable<Image> Layers(FrameworkElement thumbnail) =>
        ((Grid)((Border)((StackPanel)thumbnail).Children[0]).Child).Children.Cast<Image>();

    // One page after the other (Windows.Data.Pdf renders one page at a time), until the dialog closes.
    private static async Task RenderThumbnailsAsync(Func<int, CancellationToken, Task<(ImageSource?, ImageSource?)>> render,
        List<FrameworkElement> items, CancellationToken closed)
    {
        foreach (var item in items)
        {
            if (closed.IsCancellationRequested)
            {
                return;
            }
            try
            {
                var (background, content) = await render((int)item.Tag, closed);
                var layers = Layers(item).ToList();
                if (closed.IsCancellationRequested)
                {
                    PictureRenderer.Release(background);
                    PictureRenderer.Release(content);
                    return;
                }
                if (background != null || content != null)
                {
                    ((Border)((StackPanel)item).Children[0]).Height = double.NaN;
                }
                layers[0].Source = background;
                layers[1].Source = content;
            }
            catch (OperationCanceledException)
            {
                return; // the dialog closed
            }
            catch (Exception e)
            {
                DebugLog.Write($"insert pages: thumbnail of page {(int)item.Tag + 1}", e); // that page shows no thumbnail
            }
        }
    }
}
