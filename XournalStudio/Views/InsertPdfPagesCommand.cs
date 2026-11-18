using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Seite → Seiten aus PDF einfügen: pages of another PDF (e.g. a cover sheet) after the page in view, stored as upstream
/// Xournal++ stores a document – one background PDF, pages refer to its pages by number. The chosen pages are appended to
/// the end of the document's PDF (<see cref="PdfPageAppender"/>, the file changes at once; the dialog says so); a page of
/// the document's own PDF is just referred to, and a document without PDF takes the chosen PDF as its PDF.
/// </summary>
public sealed class InsertPdfPagesCommand(WindowId window, DocumentSession session, PagesController pages, Dialogs dialogs, Messages messages)
{
    public async Task InsertAsync()
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
        try
        {
            var sourcePdf = await PdfBackground.LoadAsync(source);
            string where = target == null
                ? $"Das Dokument verwendet dann „{Path.GetFileName(source)}“ als PDF-Hintergrund."
                : same
                    ? "Es ist das PDF des Dokuments: Die Seiten werden nur eingefügt."
                    : $"Die Seiten werden ans Ende von „{Path.GetFileName(target)}“ angehängt – das PDF des Dokuments wird " +
                      "dabei sofort geändert (wie in Xournal++ hat ein Dokument ein PDF), seine bisherigen Seiten bleiben.";
            var chosen = await AskPagesAsync(Path.GetFileName(source), sourcePdf.PageCount, where);
            if (chosen == null)
            {
                return;
            }
            if (target == null || same)
            {
                session.UsePdf(source, sourcePdf);
                pages.InsertPdfPagesAfterCurrent(chosen, sourcePdf);
            }
            else
            {
                int first = await Task.Run(() => PdfPageAppender.Append(target, source, chosen));
                var background = await PdfBackground.LoadAsync(target);
                session.UsePdf(target, background);
                pages.InsertPdfPagesAfterCurrent(Enumerable.Range(first, chosen.Count).ToList(), background);
            }
            messages.Show(InfoBarSeverity.Success, "Seiten eingefügt",
                $"{chosen.Count} Seite(n) aus {Path.GetFileName(source)}" + (target == null || same ? "." : $" – an {Path.GetFileName(target)} angehängt."));
        }
        catch (Exception e)
        {
            messages.Error("insert PDF pages", "Seiten konnten nicht eingefügt werden", e, source);
        }
    }

    // Which pages (as typed: "1", "1, 3-5", empty = all); null: canceled.
    private async Task<List<int>?> AskPagesAsync(string file, int count, string where)
    {
        var box = new TextBox { Header = $"Seiten aus „{file}“ ({count} {(count == 1 ? "Seite" : "Seiten")}; z. B. 1 oder 1, 3-5 – leer: alle)", PlaceholderText = "alle" };
        var problem = new TextBlock { Foreground = (Microsoft.UI.Xaml.Media.Brush)Microsoft.UI.Xaml.Application.Current.Resources["SystemFillColorCriticalBrush"], TextWrapping = Microsoft.UI.Xaml.TextWrapping.Wrap };
        var dialog = new ContentDialog
        {
            Title = "Seiten aus PDF einfügen",
            Content = new StackPanel { Spacing = 12, Children = { box, Dialogs.Text(where + " Sie kommen nach der Seite im Blick."), problem } },
            PrimaryButtonText = "Einfügen",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        };
        List<int>? result = null;
        void Validate()
        {
            var (parsed, message) = PageRanges.Parse(box.Text, count);
            result = parsed;
            problem.Text = message ?? "";
            dialog.IsPrimaryButtonEnabled = parsed != null;
        }
        box.TextChanged += (_, _) => Validate();
        Validate();
        return await dialogs.ShowAsync(dialog) == ContentDialogResult.Primary ? result : null;
    }
}
