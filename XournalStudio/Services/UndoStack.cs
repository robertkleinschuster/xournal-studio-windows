using System.Xml.Linq;
using Windows.UI.Input.Inking;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>
/// One change of a page: strokes and pictures removed and added. It is recorded after it was made
/// (by the InkPresenter or a tool), so <see cref="Undo"/> reverses it and <see cref="Redo"/> repeats it.
/// </summary>
public sealed class PageEdit(XoppPage page)
{
    public XoppPage Page { get; } = page;
    public List<InkStroke> RemovedStrokes { get; private set; } = new();
    public List<InkStroke> AddedStrokes { get; private set; } = new();
    public List<XElement> RemovedImages { get; } = new();
    public List<XElement> AddedImages { get; } = new();

    public bool IsEmpty => RemovedStrokes.Count + AddedStrokes.Count + RemovedImages.Count + AddedImages.Count == 0;

    public void Undo()
    {
        RemoveStrokes(Page, AddedStrokes);
        RemovedStrokes = RestoreStrokes(Page, RemovedStrokes);
        AddedImages.ForEach(Page.RemoveImage);
        RemovedImages.ForEach(Page.AddImage);
    }

    public void Redo()
    {
        RemoveStrokes(Page, RemovedStrokes);
        AddedStrokes = RestoreStrokes(Page, AddedStrokes);
        RemovedImages.ForEach(Page.RemoveImage);
        AddedImages.ForEach(Page.AddImage);
    }

    /// <summary>Removes strokes from the page's ink (they must belong to it).</summary>
    public static void RemoveStrokes(XoppPage page, IReadOnlyCollection<InkStroke> strokes)
    {
        if (strokes.Count == 0)
        {
            return;
        }
        var ink = page.Ink!;
        foreach (var s in ink.GetStrokes())
        {
            s.Selected = false;
        }
        foreach (var s in strokes)
        {
            s.Selected = true;
        }
        ink.DeleteSelected();
    }

    // A removed stroke cannot be added again; its copy takes its place (also in the file).
    private static List<InkStroke> RestoreStrokes(XoppPage page, List<InkStroke> strokes)
    {
        var copies = new List<InkStroke>(strokes.Count);
        foreach (var s in strokes)
        {
            var copy = s.Clone();
            page.Ink!.AddStroke(copy);
            page.TransferStrokeIdentity(s.Id, copy.Id);
            copies.Add(copy);
        }
        return copies;
    }
}

/// <summary>Undo/redo of page edits (the WinUI InkCanvas has no undo of its own).</summary>
public sealed class UndoStack
{
    private readonly Stack<PageEdit> undo = new();
    private readonly Stack<PageEdit> redo = new();

    public event EventHandler? Changed;

    public bool CanUndo => undo.Count > 0;
    public bool CanRedo => redo.Count > 0;

    /// <summary>Records an edit that has already been made.</summary>
    public void Push(PageEdit edit)
    {
        if (edit.IsEmpty)
        {
            return;
        }
        undo.Push(edit);
        redo.Clear();
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Clear()
    {
        undo.Clear();
        redo.Clear();
        Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Forgets the history of a deleted page.</summary>
    public void Forget(XoppPage page)
    {
        Filter(undo, page);
        Filter(redo, page);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Undo()
    {
        if (undo.TryPop(out var edit))
        {
            edit.Undo();
            redo.Push(edit);
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    public void Redo()
    {
        if (redo.TryPop(out var edit))
        {
            edit.Redo();
            undo.Push(edit);
            Changed?.Invoke(this, EventArgs.Empty);
        }
    }

    private static void Filter(Stack<PageEdit> stack, XoppPage page)
    {
        var kept = stack.Where(e => e.Page != page).Reverse().ToList();
        stack.Clear();
        foreach (var e in kept)
        {
            stack.Push(e);
        }
    }
}
