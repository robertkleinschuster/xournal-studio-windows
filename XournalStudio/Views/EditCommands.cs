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
        page.RemoveImages(images);
        edit.RemovedImages.AddRange(images);
        selection.Clear();
        undo.Push(edit);
        return true;
    }

    /// <summary>
    /// Copies the selection. Strokes alone go through Windows ink's clipboard format (other ink apps paste them too);
    /// with pictures or a PDF area the group goes in the app's own format, plus a bitmap for other apps: the area as
    /// the PDF shows it, or the one picture.
    /// </summary>
    public async Task<bool> CopyAsync()
    {
        var current = selection.Current;
        if (current == null)
        {
            return false;
        }
        try
        {
            if (current.Area == null && current.Images.Count == 0)
            {
                PageEdit.SelectOnly(current.Page, current.Strokes);
                current.Page.Ink!.CopySelectedToClipboard();
                current.Strokes.ForEach(s => s.Selected = false);
                return true;
            }
            var elements = new List<XElement>();
            byte[]? bitmap = null;
            if (current.Area is { } area)
            {
                var (png, width, height) = await current.View.Pdf!.RenderAreaAsync(current.Page.PdfPageIndex, area, PdfAreaPixelsPerPoint);
                elements.Add(XoppImage.Create(area, png, width, height));
                bitmap = png;
            }
            elements.AddRange(current.Images.Select(i => new XElement(i)));
            elements.AddRange(current.Strokes.Select(current.Page.ElementOf));
            if (bitmap == null && current.Strokes.Count == 0 && current.Images.Count == 1 &&
                !XoppImage.IsPdf(XoppImage.GetData(current.Images[0])))
            {
                bitmap = XoppImage.GetData(current.Images[0]);
            }
            await ClipboardService.CopyAsync(XoppClip.Create(current.Bounds, elements), bitmap);
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
        if (selection.Current is { HasContent: true } && await CopyAsync())
        {
            Delete();
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
            Add(view, XoppClip.Place(clip, place));
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
        if (current == null || !current.HasContent)
        {
            return;
        }
        var offset = Matrix3x2.CreateTranslation((float)Grid, (float)Grid);
        var elements = current.Images.Select(i => XoppClip.Transformed(i, offset))
            .Concat(current.Strokes.Select(s => XoppClip.Transformed(current.Page.ElementOf(s), offset)))
            .ToList();
        Add(current.View, elements);
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

    // Adds stroke and picture elements to the page (undoable) and selects them.
    private void Add(PageView view, List<XElement> elements)
    {
        var page = view.Page;
        var edit = new PageEdit(page);
        var strokes = new List<InkStroke>();
        foreach (var e in elements)
        {
            if (e.Name == "stroke")
            {
                if (page.AddStrokeElement(e) is { } stroke)
                {
                    strokes.Add(stroke);
                }
            }
            else if (XoppImage.IsImage(e))
            {
                edit.AddedImages.Add(e);
            }
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
