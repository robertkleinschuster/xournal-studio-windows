namespace InkPages;

/// <summary>The toolbar's texts (names, tooltips); an app sets them in its language. English by default.</summary>
public sealed record PenToolbarTexts
{
    public string Pen { get; init; } = "Pen";
    public string Highlighter { get; init; } = "Highlighter";
    public string Eraser { get; init; } = "Stroke eraser";
    public string EraserTip { get; init; } =
        "Stroke eraser: erases whole strokes (the pen's eraser end too, while this eraser was chosen last)";
    public string PointEraser { get; init; } = "Point eraser";
    public string PointEraserTip { get; init; } =
        "Point eraser: erases exactly the circle at the pen (the pen's eraser end too, while this eraser was chosen last); tap again for its size";
    /// <summary>The point eraser's size (its options).</summary>
    public string EraserRadius { get; init; } = "Size";
    public string Select { get; init; } = "Select";
    public string SelectTip { get; init; } =
        "Select with lasso or rectangle (tap again to choose); drag to move, corners to scale";
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
