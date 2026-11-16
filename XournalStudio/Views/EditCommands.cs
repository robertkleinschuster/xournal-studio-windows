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

    /// <summary>Deletes the selected strokes and pictures (undoable). False: nothing selected.</summary>
    public bool Delete()
    {
        var current = selection.Current;
        if (current == null || !current.HasContent)
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
        if (current.IsPreviewing)
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
        var copied = selection.Current;
        if (copied is { HasContent: false })
        {
            messages.Show(InfoBarSeverity.Informational, "Nur kopieren", "Ein PDF-Bereich lässt sich kopieren, nicht ausschneiden.");
            return;
        }
        if (copied != null && await CopyAsync() && selection.Current == copied)
        {
            Delete(); // only if the selection is still the one copied (a tap may have changed it meanwhile)
        }
    }

    /// <summary>Pastes the app's own group, else ink (Windows' format), else a bitmap (as a picture).</summary>
    public async Task PasteAsync()
    {
        var view = pages.CurrentView;
        if (view == null)
        {
            return;
        }
        try
        {
            var clip = await ClipboardService.GetClipAsync();
            if (clip == null && view.Page.Ink!.CanPasteFromClipboard())
            {
                PasteInk(view);
                return;
            }
            clip ??= await ClipboardService.GetBitmapAsClipAsync();
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
        if (current == null || !current.HasContent || current.IsPreviewing)
        {
            return;
        }
        var offset = Matrix3x2.CreateTranslation((float)Grid, (float)Grid);
        var elements = current.Images.Select(i => XoppClip.Transformed(i, offset))
            .Concat(current.Strokes.Select(s => XoppClip.Transformed(current.Page.ElementOf(s), offset)))
            .ToList();
        Add(current.View, elements, "duplicate");
    }

    /// <summary>Selects everything on the page in view.</summary>
    public void SelectAll()
    {
        var view = pages.CurrentView;
        if (view == null)
        {
            return;
        }
        activateSelectTool();
        selection.Select(view, view.Page.Ink!.GetStrokes().ToList(), view.Page.Images.ToList());
    }

    // Adds stroke and picture elements to the page (undoable) and selects them; operation names it in the log.
    // (Nothing here throws on file content: reading strokes and pictures is guarded, so a paste is never half done.)
    private void Add(PageView view, List<XElement> elements, string operation)
    {
        var page = view.Page;
        var edit = new PageEdit(page);
        var strokes = new List<InkStroke>();
        int skipped = 0;
        foreach (var e in elements)
        {
            if (e.Name == "stroke")
            {
                if (page.AddStrokeElement(e) is { } stroke)
                {
                    strokes.Add(stroke);
                }
                else
                {
                    skipped++;
                }
            }
            else if (XoppImage.IsImage(e))
            {
                edit.AddedImages.Add(e);
            }
            else
            {
                skipped++; // only in a clip from elsewhere: copies of this app hold strokes and pictures only
            }
        }
        if (skipped > 0)
        {
            // What cannot be added (from a clip of elsewhere: strokes that cannot be ink, elements this app does not
            // place; for a duplicate it would be an error of this app) – said, not silently dropped.
            DebugLog.Write($"{operation}: {skipped} of {elements.Count} element(s) not added (strokes that cannot be ink, or not placeable)");
            messages.Show(InfoBarSeverity.Warning, "Nicht alles übernommen",
                $"{skipped} Element(e) lassen sich hier nicht als Tinte oder Bild übernehmen.");
        }
        if (strokes.Count == 0 && edit.AddedImages.Count == 0)
        {
            // Nothing added: no undo step; the selection and tool stay as they were.
            DebugLog.Write($"{operation}: nothing added ({elements.Count} element(s))");
            return;
        }
        page.AddImages(edit.AddedImages);
        edit.AddedStrokes.AddRange(strokes);
        undo.Push(edit);
        activateSelectTool();
        selection.Select(view, strokes, edit.AddedImages.ToList());
    }

    private void PasteInk(PageView view)
    {
        var ink = view.Page.Ink!;
        var before = ink.GetStrokes().Select(s => s.Id).ToHashSet();
        ink.PasteFromClipboard(new Point(Grid, PasteTop(view.Page, 0)));
        var added = ink.GetStrokes().Where(s => !before.Contains(s.Id)).ToList();
        // Its height is only known now: below the page's end it goes to the top instead (as pictures do).
        var bounds = Rect.Empty;
        added.ForEach(s => bounds.Union(s.BoundingRect));
        if (!bounds.IsEmpty && bounds.Bottom > view.Page.Height)
        {
            var up = Matrix3x2.CreateTranslation(0, (float)(Grid - bounds.Top));
            added.ForEach(s => s.PointTransform *= up);
        }
        added.ForEach(s => s.Selected = false);
        var edit = new PageEdit(view.Page);
        edit.AddedStrokes.AddRange(added);
        undo.Push(edit);
        activateSelectTool();
        selection.Select(view, added, []);
    }

    private static double PasteTop(XoppPage page, double height)
    {
        double top = Math.Ceiling((page.ContentBottom() + 4) / Grid) * Grid;
        return top < Grid || top + height > page.Height ? Grid : top;
    }
}
