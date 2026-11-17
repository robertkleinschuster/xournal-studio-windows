namespace InkPages;

/// <summary>The toolbar's texts (names, tooltips); an app sets them in its language. English by default.</summary>
public sealed record PenToolbarTexts
{
    public string Pen { get; init; } = "Pen";
    public string Highlighter { get; init; } = "Highlighter";
    /// <summary>A pen or highlighter preset's tooltip after its name.</summary>
    public string PresetTip { get; init; } = "tap again for color and size";
    public string Eraser { get; init; } = "Eraser";
    public string EraserTip { get; init; } = "Eraser – the pen's eraser button erases too, with any tool";
    /// <summary>The eraser's mode toggle (exactly the circle at the pen instead of whole strokes).</summary>
    public string PartialErase { get; init; } = "Erase exactly";
    public string PartialEraseTip { get; init; } =
        "Erase exactly: the eraser and the pen's eraser button erase only what is in the circle, not whole strokes. " +
        "Right-click (pen: tap with the barrel button held): size";
    /// <summary>The partial eraser's size (the mode toggle's context flyout).</summary>
    public string EraserRadius { get; init; } = "Size";
    public string Select { get; init; } = "Select";
    public string SelectTip { get; init; } =
        "Select: lasso or rectangle (tap again to choose); drag to move, corners to scale. " +
        "With the pen, a lasso works with any tool: drag with the barrel button held";
    public string SelectWith { get; init; } = "Select with";
    public string Lasso { get; init; } = "Lasso";
    public string Rectangle { get; init; } = "Rectangle";
    public string Shapes { get; init; } = "Shapes";
    public string ShapesTip { get; init; } = "Shapes: line, arrow, rectangle, ellipse (tap again to choose)";
    public string Shape { get; init; } = "Shape";
    /// <summary>The header above shape recognition (in the shape tool's options).</summary>
    public string Freehand { get; init; } = "Drawn freehand";
    public IReadOnlyList<string> ShapeKinds { get; init; } = new[] { "Line", "Arrow", "Rectangle", "Ellipse" };
    public string Ruler { get; init; } = "Ruler / protractor";
    public string RecognizeShapes { get; init; } = "Recognize drawn shapes";
    public string RecognizeShapesTip { get; init; } = "Shapes drawn with the pen (circle, rectangle, ...) become clean shapes";
    // (Keyboard shortcuts are the app's: it names them in its texts.)
    public string Undo { get; init; } = "Undo";
    public string Redo { get; init; } = "Redo";
    public string Cut { get; init; } = "Cut selection";
    public string Copy { get; init; } = "Copy selection";
    public string Duplicate { get; init; } = "Duplicate selection";
    public string DeleteSelection { get; init; } = "Delete selection";
}
