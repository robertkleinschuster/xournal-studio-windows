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
/// selection tool active), ready to be moved, scaled, duplicated or deleted.
/// Pasting goes to the page in view, at the left margin below what is already on it (on the next grid line), so parts
/// of an exercise stack up on a graph page; wider than the page, it is scaled down to fit.
/// </summary>
public sealed class EditCommands(
    SelectionTool selection, PagesController pages, UndoStack undo, Messages messages, Action activateSelectTool)
{
    private const double Grid = 14.17;              // Xournal's graph spacing (5 mm)
    private const double PdfAreaPixelsPerPoint = 3; // 216 dpi: sharp text when pasted at the original size

    /// <summary>
    /// The one rule while the selection is being moved or scaled (<see cref="SelectionTool.IsDragging"/>): a command
    /// that reads or changes content is refused and the user is told – the move is never lost or measured mid-way.
    /// True: refused. Checked when a command starts and again after it waited (clipboard, rendering).
    /// </summary>
    public bool RefusedWhileDragging(string command)
    {
        if (!selection.IsDragging)
        {
            return false;
        }
        messages.Show(InfoBarSeverity.Informational, $"{command}: nicht ausgeführt", "Erst loslassen – die Auswahl wird gerade verschoben.");
        return true;
    }

    /// <summary>Deletes the selected strokes and pictures (undoable). False: nothing selected (or refused mid-drag).</summary>
    public bool Delete()
    {
        var current = selection.Current;
        if (current == null || !current.HasContent || RefusedWhileDragging("Löschen"))
        {
            return false;
        }
        var page = current.Page;
        var edit = new PageEdit(page);
        // Only what is still on the page (the eraser may have taken a selected stroke meanwhile).
        edit.RemovedStrokes.AddRange(PageEdit.SelectOnly(page, current.Strokes));
        page.Ink.StrokeContainer.DeleteSelected();
        var images = PicturesOf(current);
        if (images.Count > 0)
        {
            edit.RemovePictures(images);
        }
        selection.Clear();
        undo.Push(edit); // (an empty edit – everything selected was erased meanwhile – is not recorded)
        return !edit.IsEmpty;
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
        if (RefusedWhileDragging("Kopieren"))
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
        if (RefusedWhileDragging("Ausschneiden"))
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
        if (await CopyAsync() && selection.Current == copied && !RefusedWhileDragging("Ausschneiden (kopiert)"))
        {
            Delete(); // only if the selection is still the one copied (a tap may have changed it meanwhile)
        }
    }

    /// <summary>Pastes the app's own group, else ink (Windows' format), else a bitmap (as a picture).</summary>
    public async Task PasteAsync()
    {
        var view = pages.CurrentView;
        if (view == null || RefusedWhileDragging("Einfügen"))
        {
            return; // mid-drag: the place below the content and the dragged content itself are not settled
        }
        try
        {
            var clip = await ClipboardService.GetClipAsync();
            if (RefusedWhileDragging("Einfügen"))
            {
                return; // a drag began while the clipboard was read
            }
            if (clip == null && view.Page.Ink!.CanPasteFromClipboard())
            {
                PasteInk(view);
                return;
            }
            clip ??= await ClipboardService.GetBitmapAsClipAsync();
            if (RefusedWhileDragging("Einfügen"))
            {
                return;
            }
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
        if (current == null || !current.HasContent || RefusedWhileDragging("Duplizieren"))
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
        var view = pages.CurrentView;
        if (view == null || RefusedWhileDragging("Alles auswählen"))
        {
            return;
        }
        activateSelectTool();
        selection.Select(view, view.Ink.StrokeContainer.GetStrokes().ToList(), view.Pictures.ToList());
    }

    /// <summary>
    /// Hands a dragged selection over to another page: its elements, transformed by <paramref name="m"/> from where
    /// they were before the drag into the target's coordinates, are added to the target, then taken from their page.
    /// Returns what was added and the edit of both pages (not recorded: the drag records itself), or null when nothing
    /// moved (the user was told) – all or nothing.
    /// </summary>
    public (List<InkStroke> Strokes, List<object> Pictures, IPageEdit Edit)? TransferToPage(Selection current,
        InkPage target, Matrix3x2 m)
    {
        List<InkStroke> strokes = [];
        try
        {
            current.Revert(); // the elements as before the drag (m includes the move)
            var source = (PageView)current.Page;
            strokes = PageEdit.SelectOnly(source, current.Strokes); // what is still on the page (now marked)
            var images = PicturesOf(current);
            var elements = images.Cast<XElement>().Select(i => XoppClip.Transformed(i, m))
                .Concat(strokes.Select(s => XoppClip.Transformed(source.Page.ElementOf(s), m))).ToList();
            if (AddElements((PageView)target, elements, "move to page", allOrNothing: true) is not { } adding)
            {
                strokes.ForEach(s => s.Selected = false);
                return null; // nothing could be added (said): the content stays where it was
            }
            var removal = new PageEdit(source);
            removal.RemovedStrokes.AddRange(strokes);
            source.Ink.StrokeContainer.DeleteSelected(); // the strokes marked above
            if (images.Count > 0)
            {
                removal.RemovePictures(images);
            }
            return (adding.AddedStrokes.ToList(), adding.AddedPictures.ToList(), new CombinedEdit([removal, adding]));
        }
        catch (Exception e)
        {
            strokes.ForEach(s => s.Selected = false);
            messages.Error("move to page", "Verschieben auf die andere Seite nicht möglich", e);
            return null;
        }
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
    // taken back before the error is reported. Null: nothing was added (the user was told) – with allOrNothing also
    // when an element could not be added (a move must not lose what stays behind).
    private PageEdit? AddElements(PageView view, List<XElement> elements, string operation, bool allOrNothing = false)
    {
        var page = view.Page;
        var edit = new PageEdit(view);
        int skipped = 0;
        try
        {
            foreach (var e in elements)
            {
                if (e.Name == "stroke")
                {
                    if (page.AddStrokeElement(e) is { } stroke)
                    {
                        edit.AddedStrokes.Add(stroke);
                    }
                    else
                    {
                        skipped++;
                    }
                }
                else if (XoppImage.IsImage(e))
                {
                    edit.AddedPictures.Add(e); // recorded before attached: a take-back finds it
                }
                else
                {
                    skipped++; // an element this app does not place
                }
            }
            page.AddImages(edit.AddedPictures.Cast<XElement>().ToList());
        }
        catch
        {
            TakeBack(edit); // nothing half added; the caller reports the original error
            throw;
        }
        bool nothing = edit.AddedStrokes.Count == 0 && edit.AddedPictures.Count == 0;
        if (skipped > 0 || nothing)
        {
            // Said, not silently dropped: strokes that cannot be ink (drawn from a file only) and elements this app
            // does not place (from a clip of elsewhere; for a duplicate an error of this app).
            DebugLog.Write($"{operation}: {skipped} of {elements.Count} element(s) not added");
            if (allOrNothing && skipped > 0)
            {
                TakeBack(edit);
                messages.Show(InfoBarSeverity.Warning, "Nicht verschoben",
                    $"{skipped} Element(e) lassen sich dort nicht als Tinte oder Bild übernehmen; alles bleibt auf seiner Seite.");
                return null;
            }
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
            activateSelectTool();
            selection.Select(view, strokes, images);
        }
        catch (Exception e)
        {
            messages.Error($"{operation}: select the added content",
                operation == "duplicate" ? "Dupliziert, aber nicht ausgewählt" : "Eingefügt, aber nicht ausgewählt", e);
        }
    }

    private void PasteInk(PageView view)
    {
        var ink = view.Ink.StrokeContainer;
        var before = ink.GetStrokes().Select(s => s.Id).ToHashSet();
        var edit = new PageEdit(view);
        try
        {
            ink.PasteFromClipboard(new Point(Grid, PasteTop(view.Page, 0)));
            edit.AddedStrokes.AddRange(ink.GetStrokes().Where(s => !before.Contains(s.Id)));
            if (edit.AddedStrokes.Count == 0)
            {
                DebugLog.Write("paste ink: the clipboard's ink added no strokes");
                ReportNothingAdded("Die Tinte in der Zwischenablage enthält keine Striche.");
                return;
            }
            // Its height is only known now: below the page's end it goes to the top instead (as pictures do).
            var bounds = Rect.Empty;
            edit.AddedStrokes.ForEach(s => bounds.Union(s.BoundingRect));
            if (bounds.Bottom > view.Page.Height)
            {
                var up = Matrix3x2.CreateTranslation(0, (float)(Grid - bounds.Top));
                edit.AddedStrokes.ForEach(s => s.PointTransform *= up);
            }
            edit.AddedStrokes.ForEach(s => s.Selected = false);
        }
        catch
        {
            // Strokes Windows ink added before it failed are taken back too (found by id); the caller reports the
            // original error, nothing here may replace it.
            try
            {
                var found = ink.GetStrokes().Where(s => !before.Contains(s.Id)).ToList(); // the list stays if this fails
                edit.AddedStrokes.Clear();
                edit.AddedStrokes.AddRange(found);
            }
            catch (Exception e)
            {
                DebugLog.Write("paste ink: finding the strokes to take back", e);
            }
            TakeBack(edit);
            throw;
        }
        undo.Push(edit);
        SelectAdded(view, edit.AddedStrokes.ToList(), [], "paste ink");
    }

    // Nothing was added: no undo step, the selection and tool stay as they were – and the user is told.
    private void ReportNothingAdded(string why) => messages.Show(InfoBarSeverity.Warning, "Nichts übernommen", why);

    // Takes back what a paste or duplicate that failed had added: its strokes, and its pictures if they were attached
    // (nothing else an undo does applies). An error doing so is logged; the original one is what the caller reports.
    private static void TakeBack(PageEdit edit)
    {
        try
        {
            PageEdit.RemoveStrokes(edit.Page, edit.AddedStrokes);
        }
        catch (Exception e)
        {
            DebugLog.Write("taking back the strokes of a failed add", e);
        }
        var attached = edit.AddedPictures.Cast<XElement>().Where(i => i.Parent != null).ToList();
        if (attached.Count > 0)
        {
            try
            {
                ((PageView)edit.Page).Page.RemoveImages(attached);
            }
            catch (Exception e)
            {
                DebugLog.Write("taking back the pictures of a failed add", e);
            }
        }
    }

    // The selected pictures still on their page (a picture of a selection may have been taken meanwhile).
    private static List<object> PicturesOf(Selection selection) =>
        selection.Pictures.Cast<XElement>().Where(i => i.Parent != null).Cast<object>().ToList();

    private static double PasteTop(XoppPage page, double height)
    {
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        return top < Grid || top + height > page.Height ? Grid : top;
    }
}
