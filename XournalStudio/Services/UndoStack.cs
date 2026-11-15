using System.Xml.Linq;
using Windows.Foundation;
using Windows.UI.Input.Inking;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>A change of one page that can be undone and redone. Recorded after it was made.</summary>
public interface IPageEdit
{
    XoppPage Page { get; }

    bool IsEmpty { get; }

    void Undo();

    void Redo();
}

/// <summary>
/// Strokes and pictures removed and added (by the InkPresenter or a tool), so <see cref="Undo"/> reverses it and
/// <see cref="Redo"/> repeats it.
/// </summary>
public sealed class PageEdit(XoppPage page) : IPageEdit
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
        Page.RemoveImages(AddedImages);
        Page.AddImages(RemovedImages);
    }

    public void Redo()
    {
        RemoveStrokes(Page, RemovedStrokes);
        AddedStrokes = RestoreStrokes(Page, AddedStrokes);
        Page.RemoveImages(RemovedImages);
        Page.AddImages(AddedImages);
    }

    /// <summary>Removes strokes (or the copies undo/redo put in their place) from the page's ink.</summary>
    public static void RemoveStrokes(XoppPage page, IReadOnlyCollection<InkStroke> strokes)
    {
        if (strokes.Count == 0)
        {
            return;
        }
        SelectOnly(page, strokes);
        page.Ink!.DeleteSelected();
    }

    /// <summary>Marks exactly these strokes (as they are on the page now) as selected; returns them.</summary>
    public static List<InkStroke> SelectOnly(XoppPage page, IEnumerable<InkStroke> strokes)
    {
        foreach (var s in page.Ink!.GetStrokes())
        {
            s.Selected = false;
        }
        var current = page.CurrentAll(strokes).OfType<InkStroke>().ToList();
        current.ForEach(s => s.Selected = true);
        return current;
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

/// <summary>
/// Strokes and pictures moved/scaled (a selection dragged or resized): undo and redo put back the exact placement
/// before and after (no inverse transform, so the file gets back exactly its numbers).
/// </summary>
public sealed class TransformEdit(XoppPage page, Placement before, Placement after) : IPageEdit
{
    public XoppPage Page { get; } = page;

    public bool IsEmpty => before.Strokes.Count + before.Images.Count == 0;

    public void Undo() => Page.RestorePlacement(before);

    public void Redo() => Page.RestorePlacement(after);
}

/// <summary>Undo/redo of page edits (the WinUI InkCanvas has no undo of its own).</summary>
public sealed class UndoStack
{
    private readonly Stack<IPageEdit> undo = new();
    private readonly Stack<IPageEdit> redo = new();

    public event EventHandler? Changed;

    public bool CanUndo => undo.Count > 0;
    public bool CanRedo => redo.Count > 0;

    /// <summary>Records an edit that has already been made.</summary>
    public void Push(IPageEdit edit)
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

    private static void Filter(Stack<IPageEdit> stack, XoppPage page)
    {
        var kept = stack.Where(e => e.Page != page).Reverse().ToList();
        stack.Clear();
        foreach (var e in kept)
        {
            stack.Push(e);
        }
    }
}
