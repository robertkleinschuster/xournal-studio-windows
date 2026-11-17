namespace InkPages;

/// <summary>The toolbar's texts (names, tooltips); an app sets them in its language. English by default.</summary>
public sealed record PenToolbarTexts
{
    public string Pen { get; init; } = "Pen";
    public string Highlighter { get; init; } = "Highlighter";
    public string Eraser { get; init; } = "Eraser";
    public string PartialEraser { get; init; } = "Partial eraser";
    public string PartialEraserTip { get; init; } =
        "Partial eraser: the eraser (and the pen's eraser end) erases exactly the circle at the pen. Size: right-click or press and hold.";
    public string EraserRadius { get; init; } = "Radius (points)";
    public string Select { get; init; } = "Select";
    public string SelectTip { get; init; } =
        "Select with lasso or rectangle (tap again to choose); drag to move, corners to scale";
    public string SelectWith { get; init; } = "Select with";
    public string Lasso { get; init; } = "Lasso";
    public string Rectangle { get; init; } = "Rectangle";
    public string Shapes { get; init; } = "Shapes";
    public string ShapesTip { get; init; } = "Shapes: line, arrow, rectangle, ellipse (tap again to choose)";
    public string Shape { get; init; } = "Shape";
    public IReadOnlyList<string> ShapeKinds { get; init; } = new[] { "Line", "Arrow", "Rectangle", "Ellipse" };
    public string Ruler { get; init; } = "Ruler / protractor";
    public string RecognizeShapes { get; init; } = "Recognize shapes";
    public string Undo { get; init; } = "Undo (Ctrl+Z)";
    public string Redo { get; init; } = "Redo (Ctrl+Y)";
    public string Cut { get; init; } = "Cut selection (Ctrl+X)";
    public string Copy { get; init; } = "Copy selection (Ctrl+C)";
    public string Duplicate { get; init; } = "Duplicate selection (Ctrl+D)";
    public string DeleteSelection { get; init; } = "Delete selection (Del)";
}
