using Microsoft.UI.Xaml.Controls;

namespace XournalStudio.Views;

/// <summary>
/// The page commands (insert, insert from a PDF, move, delete – from the menu, the toolbar and the pages pane) and the
/// pages pane itself: it shows the document's pages, their thumbnails follow edits, and the selected page follows the page
/// in view.
/// </summary>
public sealed class PageCommands
{
    private readonly PagesController pages;
    private readonly DocumentSession session;
    private readonly InkWorkspace ink;
    private readonly PagesPane pane;
    private readonly InsertPdfPagesCommand insertPdf;
    private readonly Dialogs dialogs;
    private readonly Messages messages;

    public PageCommands(PagesController pages, DocumentSession session, InkWorkspace ink, PagesPane pane,
        PageThumbnails thumbnails, InsertPdfPagesCommand insertPdf, Dialogs dialogs, Messages messages)
    {
        this.pages = pages;
        this.session = session;
        this.ink = ink;
        this.pane = pane;
        this.insertPdf = insertPdf;
        this.dialogs = dialogs;
        this.messages = messages;

        pages.DocumentShown += (_, _) => ShowPane(newDocument: true);
        pages.PagesChanged += (_, _) => ShowPane(newDocument: false);
        pages.CurrentPageChanged += (_, _) => pane.Select(pages.CurrentIndex);
        ink.Undo.ContentEdited += (_, edited) => thumbnails.Edited(edited); // the pages an edit touched are drawn again
        pane.GoToRequested += (_, index) => pages.ScrollToPage(index);
        pane.InsertBlankRequested += (_, index) => pages.InsertBlankAt(index);
        pane.InsertPdfRequested += async (_, index) => await insertPdf.InsertAsync(index);
        pane.MoveRequested += (_, move) => Move(move.From, move.To);
        pane.DeleteRequested += async (_, index) => await DeleteAsync(index);
    }

    /// <summary>A blank page after the page in view.</summary>
    public void InsertAfterCurrent() => pages.InsertAfterCurrent();

    /// <summary>Pages of a PDF around the page in view (the dialog asks before or after).</summary>
    public Task InsertPdfPagesAsync() => insertPdf.InsertAsync(pages.CurrentIndex);

    /// <summary>Deletes the page in view after asking.</summary>
    public Task DeleteCurrentAsync() => DeleteAsync(pages.CurrentIndex);

    private void Move(int from, int to)
    {
        if (pages.Move(from, to))
        {
            return;
        }
        ShowPane(newDocument: false); // the list showed the drag's order: back to the document's
        if (ink.Selection.IsDragging)
        {
            messages.Show(InfoBarSeverity.Informational, "Seite nicht verschoben", PenToolbarTexts.German.Refused);
        }
    }

    // Deletes a page after asking – always (user, 2026-11-17): deleting a page cannot be undone (its history goes with it).
    private async Task DeleteAsync(int index)
    {
        var doc = session.Document;
        if (doc.Pages.Count <= 1)
        {
            messages.Show(InfoBarSeverity.Informational, "Seite nicht gelöscht", "Ein Dokument behält mindestens eine Seite.");
            return;
        }
        if (index < 0 || index >= doc.Pages.Count)
        {
            return;
        }
        var page = doc.Pages[index];
        string content = page.HasContent ? "Die Seite enthält Notizen. " : "";
        if (await dialogs.ConfirmAsync($"Seite {index + 1} löschen?",
                content + "Das Löschen einer Seite lässt sich nicht rückgängig machen.", "Löschen"))
        {
            pages.Remove(page);
        }
    }

    private void ShowPane(bool newDocument) =>
        pane.Show(session.Document.Pages, () => pages.Count == 0 ? -1 : pages.CurrentIndex, newDocument);
}
