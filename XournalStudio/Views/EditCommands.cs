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
        page.Ink!.DeleteSelected();
        var images = current.Images.Where(i => i.Parent != null).ToList();
        edit.RemoveImages(images);
        selection.Clear();
        if (edit.IsEmpty)
        {
            return false; // everything selected was erased meanwhile: no undo step, nothing changed
        }
        undo.Push(edit);
        return true;
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
            var elements = current.Images.Select(i => new XElement(i))
                .Concat(current.Strokes.Select(current.Page.ElementOf)).ToList();
            byte[]? bitmap = null;
            if (area is { } a)
            {
                var (png, width, height) = await current.View.Pdf!.RenderAreaAsync(current.Page.PdfPageIndex, a, PdfAreaPixelsPerPoint);
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
            messages.Show(InfoBarSeverity.Informational, "Nichts ausgewählt", "Erst etwas auswählen, dann ausschneiden.");
            return;
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
            var elements = current.Images.Select(i => XoppClip.Transformed(i, offset))
                .Concat(current.Strokes.Select(s => XoppClip.Transformed(current.Page.ElementOf(s), offset)))
                .ToList();
            Add(current.View, elements, "duplicate");
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
        selection.Select(view, view.Page.Ink!.GetStrokes().ToList(), view.Page.Images.ToList());
    }

    // Adds stroke and picture elements to the page (undoable) and selects them; operation names it in the log. All or
    // nothing: if adding fails, what was added is taken back before the error is reported.
    private void Add(PageView view, List<XElement> elements, string operation)
    {
        var page = view.Page;
        var edit = new PageEdit(page);
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
                    edit.AddedImages.Add(e); // recorded before attached: a take-back finds it
                }
                else
                {
                    skipped++; // an element this app does not place
                }
            }
            page.AddImages(edit.AddedImages);
        }
        catch
        {
            TakeBack(edit); // nothing half added; the caller reports the original error
            throw;
        }
        bool nothing = edit.AddedStrokes.Count == 0 && edit.AddedImages.Count == 0;
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
                return;
            }
            messages.Show(InfoBarSeverity.Warning, "Nicht alles übernommen", why);
        }
        undo.Push(edit);
        SelectAdded(view, edit.AddedStrokes.ToList(), edit.AddedImages.ToList(), operation);
    }

    // Selects what was just added (and undoable). A failure here is not a failed paste or duplicate: logged and said
    // as what it is.
    private void SelectAdded(PageView view, List<InkStroke> strokes, List<XElement> images, string operation)
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
        var ink = view.Page.Ink!;
        var before = ink.GetStrokes().Select(s => s.Id).ToHashSet();
        var edit = new PageEdit(view.Page);
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
        var attached = edit.AddedImages.Where(i => i.Parent != null).ToList();
        if (attached.Count > 0)
        {
            try
            {
                edit.Page.RemoveImages(attached);
            }
            catch (Exception e)
            {
                DebugLog.Write("taking back the pictures of a failed add", e);
            }
        }
    }

    private static double PasteTop(XoppPage page, double height)
    {
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        return top < Grid || top + height > page.Height ? Grid : top;
    }
}
