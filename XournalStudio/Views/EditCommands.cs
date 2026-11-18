using System.Numerics;
using System.Xml.Linq;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using XournalStudio.Model;
using XournalStudio.Services;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace XournalStudio.Views;

/// <summary>
/// Cut, copy, paste, duplicate, delete and select all. Whatever is pasted or duplicated ends up selected (with the
/// selection tool active), ready to be moved, scaled, duplicated or deleted. Delete, select all, pasting Windows ink and
/// selecting what was added are the library's (<see cref="InkCommands"/>); copy, cut, paste and duplicate of the file's
/// own elements (pictures, PDF areas) are this app's.
/// Pasting goes to the page in view, at the left margin below what is already on it (on the next grid line), so parts
/// of an exercise stack up on a graph page; wider than the page, it is scaled down to fit.
/// </summary>
public sealed class EditCommands(
    InkCommands commands, SelectionTool selection, PagesController pages, UndoStack undo, Messages messages)
{
    private const double Grid = 14.17;              // Xournal's graph spacing (5 mm)
    private const double PdfAreaPixelsPerPoint = 3; // 216 dpi: sharp text when pasted at the original size

    /// <summary>
    /// The library's one rule (<see cref="InkCommands.Refuses"/>): while the selection is being moved or scaled, or the
    /// ink is hidden under a dialog, a command that reads or changes content is refused and the user is told
    /// (<see cref="ReportRefused"/>) – the move is never lost or measured mid-way. True: refused. Checked when a command
    /// starts and again after it waited (clipboard, rendering).
    /// </summary>
    public bool Refused(InkCommand command) => commands.Refuses(command);

    /// <summary>Tells the user that a command waits for the drag (the library's <see cref="InkCommands.Refused"/>).</summary>
    public void ReportRefused(InkCommand command) =>
        messages.Show(InfoBarSeverity.Informational, $"{Name(command)}: nicht ausgeführt", PenToolbarTexts.German.Refused);

    private static string Name(InkCommand command) => command switch
    {
        InkCommand.Undo => "Rückgängig",
        InkCommand.Redo => "Wiederholen",
        InkCommand.Cut => "Ausschneiden",
        InkCommand.Copy => "Kopieren",
        InkCommand.Paste => "Einfügen",
        InkCommand.Duplicate => "Duplizieren",
        InkCommand.Delete => "Löschen",
        InkCommand.SelectAll => "Alles auswählen",
        InkCommand.ClearPage => "Seite leeren",
        _ => command.ToString(),
    };

    /// <summary>A dragged selection could not go on to another page (the library's <see cref="SelectionTool.TransferFailed"/>).</summary>
    public void ReportTransferFailed(Exception? e)
    {
        if (e != null)
        {
            messages.Error("move to page", "Verschieben auf die andere Seite nicht möglich", e);
        }
        else
        {
            messages.Show(InfoBarSeverity.Warning, "Nicht verschoben",
                "Nicht alles lässt sich dort als Tinte oder Bild übernehmen; alles bleibt auf seiner Seite.");
        }
    }

    /// <summary>Deletes the selected strokes and pictures (undoable). False: nothing selected (or refused mid-drag).</summary>
    public bool Delete()
    {
        if (selection.Current is not { HasContent: true } || Refused(InkCommand.Delete))
        {
            return false;
        }
        return commands.Delete();
    }

    /// <summary>
    /// Copies the selection in the app's own format – the .xopp elements themselves, so a copy or a cut and paste keeps
    /// exactly what the file had – plus a bitmap for other apps: the area as the PDF shows it, or the one picture.
    /// </summary>
    public async Task<bool> CopyAsync()
    {
        var current = selection.Current;
        if (current == null)
        {
            return false;
        }
        if (Refused(InkCommand.Copy))
        {
            return false; // mid-drag the content is between two places
        }
        try
        {
            // Taken now: the selection may be moved while the area renders.
            var bounds = current.Bounds;
            var area = current.Area;
            var page = (PageView)current.Page;
            var elements = current.Pictures.Cast<XElement>().Select(i => new XElement(i))
                .Concat(current.Strokes.Select(page.Page.ElementOf)).ToList();
            byte[]? bitmap = null;
            if (area is { } a)
            {
                var (png, width, height) = await page.Pdf!.RenderAreaAsync(page.Page.PdfPageIndex, a, PdfAreaPixelsPerPoint);
                elements.Insert(0, XoppImage.Create(a, png, width, height)); // below the notes on it
                bitmap = png;
            }
            else if (elements.Count == 1 && XoppImage.IsImage(elements[0]) && !XoppImage.IsPdf(XoppImage.GetData(elements[0])))
            {
                bitmap = XoppImage.GetData(elements[0]);
            }
            await ClipboardService.CopyAsync(XoppClip.Create(bounds, elements), bitmap);
            return true;
        }
        catch (Exception e)
        {
            messages.Error("copy", "Kopieren fehlgeschlagen", e);
            return false;
        }
    }

    public async Task CutAsync()
    {
        if (Refused(InkCommand.Cut))
        {
            return;
        }
        var copied = selection.Current;
        if (copied == null)
        {
            return; // nothing selected: as copy, duplicate and delete
        }
        if (copied is { HasContent: false })
        {
            messages.Show(InfoBarSeverity.Informational, "Nur kopieren", "Ein PDF-Bereich lässt sich kopieren, nicht ausschneiden.");
            return;
        }
        if (await CopyAsync() && selection.Current == copied && !Refused(InkCommand.Cut))
        {
            Delete(); // only if the selection is still the one copied (a tap may have changed it meanwhile)
        }
    }

    /// <summary>Pastes the app's own group, else ink (Windows' format), else a bitmap (as a picture).</summary>
    public async Task PasteAsync()
    {
        var target = pages.CurrentPageView()?.Page;
        if (target == null || Refused(InkCommand.Paste))
        {
            return; // mid-drag: the place below the content and the dragged content itself are not settled
        }
        try
        {
            var clip = await ClipboardService.GetClipAsync();
            // The page's view now (taken again after waiting: a view far from the viewport may have been released).
            if (Refused(InkCommand.Paste) || pages.ViewOf(target) is not { } view)
            {
                return; // a drag began while the clipboard was read (or the page was deleted)
            }
            if (clip == null && view.Page.Ink!.CanPasteFromClipboard())
            {
                PasteInk(view);
                return;
            }
            clip ??= await ClipboardService.GetBitmapAsClipAsync();
            if (Refused(InkCommand.Paste) || pages.ViewOf(target) is not { } viewNow)
            {
                return;
            }
            view = viewNow;
            if (clip == null)
            {
                messages.Show(InfoBarSeverity.Informational, "Nichts zum Einfügen in der Zwischenablage",
                    "Etwas auswählen und kopieren, oder einen Ausschnitt aus einer anderen App (Ausschneidetool: Win+Umschalt+S).");
                return;
            }
            var size = XoppClip.GetSize(clip);
            double scale = size.Width > 0 ? Math.Min(1, (view.Page.Width - 2 * Grid) / size.Width) : 1;
            var place = Matrix3x2.CreateScale((float)scale) *
                Matrix3x2.CreateTranslation((float)Grid, (float)PasteTop(view.Page, size.Height * scale));
            Add(view, XoppClip.Place(clip, place), "paste");
        }
        catch (Exception e)
        {
            messages.Error("paste", "Einfügen nicht möglich", e);
        }
    }

    /// <summary>A copy of the selection one grid step to the right and down, selected.</summary>
    public void Duplicate()
    {
        var current = selection.Current;
        if (current == null || !current.HasContent || Refused(InkCommand.Duplicate))
        {
            return;
        }
        var offset = Matrix3x2.CreateTranslation((float)Grid, (float)Grid);
        try
        {
            var page = (PageView)current.Page;
            var elements = current.Pictures.Cast<XElement>().Select(i => XoppClip.Transformed(i, offset))
                .Concat(current.Strokes.Select(s => XoppClip.Transformed(page.Page.ElementOf(s), offset)))
                .ToList();
            Add(page, elements, "duplicate");
        }
        catch (Exception e)
        {
            messages.Error("duplicate", "Duplizieren nicht möglich", e);
        }
    }

    /// <summary>Selects everything on the page in view.</summary>
    public void SelectAll()
    {
        var view = pages.CurrentPageView();
        if (view == null || Refused(InkCommand.SelectAll))
        {
            return;
        }
        commands.SelectAll(view);
    }

    // Adds stroke and picture elements to the page (undoable) and selects them; operation names it in the log.
    private void Add(PageView view, List<XElement> elements, string operation)
    {
        if (AddElements(view, elements, operation) is { } edit)
        {
            undo.Push(edit);
            SelectAdded(view, edit.AddedStrokes.ToList(), edit.AddedPictures.ToList(), operation);
        }
    }

    // Adds stroke and picture elements to the page; the edit is not recorded yet. If adding fails, what was added is
    // taken back before the error is reported. Null: nothing was added (the user was told).
    private PageEdit? AddElements(PageView view, List<XElement> elements, string operation)
    {
        var edit = view.AddElements(elements, out int skipped);
        bool nothing = edit.AddedStrokes.Count == 0 && edit.AddedPictures.Count == 0;
        if (skipped > 0 || nothing)
        {
            // Said, not silently dropped: strokes that cannot be ink (drawn from a file only) and elements this app
            // does not place (from a clip of elsewhere; for a duplicate an error of this app).
            DebugLog.Write($"{operation}: {skipped} of {elements.Count} element(s) not added");
            string why = skipped > 0
                ? $"{skipped} Element(e) lassen sich hier nicht als Tinte oder Bild übernehmen."
                : "Es gibt nichts, was sich hier übernehmen lässt.";
            if (nothing)
            {
                ReportNothingAdded(why);
                return null;
            }
            messages.Show(InfoBarSeverity.Warning, "Nicht alles übernommen", why);
        }
        return edit;
    }

    // Selects what was just added (and undoable). A failure here is not a failed paste or duplicate: logged and said
    // as what it is.
    private void SelectAdded(PageView view, List<InkStroke> strokes, List<object> images, string operation)
    {
        try
        {
            commands.SelectAdded(view, strokes, images);
        }
        catch (Exception e)
        {
            messages.Error($"{operation}: select the added content",
                operation == "duplicate" ? "Dupliziert, aber nicht ausgewählt" : "Eingefügt, aber nicht ausgewählt", e);
        }
    }

    // Windows ink from another app (or from this one's copy as ink): at the left margin below the page's content.
    private void PasteInk(PageView view)
    {
        if (commands.PasteInk(view, new Point(Grid, PasteTop(view.Page, 0)), Grid) is { Count: 0 })
        {
            ReportNothingAdded("Die Tinte in der Zwischenablage enthält keine Striche."); // null: refused (said)
        }
    }
    // Nothing was added: no undo step, the selection and tool stay as they were – and the user is told.
    private void ReportNothingAdded(string why) => messages.Show(InfoBarSeverity.Warning, "Nichts übernommen", why);

    private static double PasteTop(XoppPage page, double height)
    {
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        return top < Grid || top + height > page.Height ? Grid : top;
    }
}
