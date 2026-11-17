using Windows.UI;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The app's set-up of the library's toolbar: German texts, Xournal++'s palette, the pen presets from the settings (one
/// tap each: black, blue, red by default), and the app's own buttons at the end.
/// </summary>
public static class AppToolbar
{
    private static readonly PenToolbarTexts German = new()
    {
        Pen = "Stift",
        Highlighter = "Textmarker",
        Eraser = "Strich-Radierer",
        EraserTip = "Strich-Radierer: löscht ganze Striche (auch mit dem Radierer-Ende des Stifts, solange er zuletzt gewählt wurde)",
        PointEraser = "Punkt-Radierer",
        PointEraserTip = "Punkt-Radierer: löscht genau den Kreis am Stift (auch mit dem Radierer-Ende, solange er zuletzt gewählt wurde); " +
            "nochmal tippen für die Größe",
        EraserRadius = "Größe",
        Select = "Auswählen",
        SelectTip = "Auswählen: Lasso oder Rechteck (nochmal tippen zum Wählen); ziehen verschiebt, Ecken skalieren; " +
            "ein Rechteck auf einer PDF-Seite kopiert diesen Bereich",
        SelectWith = "Auswählen mit",
        Lasso = "Lasso",
        Rectangle = "Rechteck",
        Shapes = "Formen",
        ShapesTip = "Formen: Linie, Pfeil, Rechteck, Ellipse (nochmal tippen zum Wählen)",
        Shape = "Form",
        Freehand = "Freihand gezeichnet",
        ShapeKinds = new[] { "Linie", "Pfeil", "Rechteck", "Ellipse" },
        Ruler = "Lineal / Geodreieck",
        RecognizeShapes = "Gezeichnete Formen erkennen",
        RecognizeShapesTip = "Mit dem Stift gezeichnete Kreise, Rechtecke, ... werden zu sauberen Formen",
        Undo = "Rückgängig (Strg+Z)",
        Redo = "Wiederholen (Strg+Y)",
        Cut = "Auswahl ausschneiden (Strg+X)",
        Copy = "Auswahl kopieren (Strg+C)",
        Duplicate = "Auswahl duplizieren (Strg+D)",
        DeleteSelection = "Auswahl löschen (Entf)",
    };

    /// <summary>Xournal++'s default palette (the pens' colors are indexes into it).</summary>
    public static readonly NamedColor[] PenColors =
    [
        new(Color.FromArgb(255, 0x00, 0x00, 0x00), "Schwarz"),
        new(Color.FromArgb(255, 0x33, 0x33, 0xcc), "Blau"),
        new(Color.FromArgb(255, 0xff, 0x00, 0x00), "Rot"),
        new(Color.FromArgb(255, 0x00, 0x80, 0x00), "Grün"),
        new(Color.FromArgb(255, 0x80, 0x80, 0x80), "Grau"),
        new(Color.FromArgb(255, 0x00, 0xc0, 0xff), "Hellblau"),
        new(Color.FromArgb(255, 0x00, 0xff, 0x00), "Hellgrün"),
        new(Color.FromArgb(255, 0xff, 0x00, 0xff), "Magenta"),
        new(Color.FromArgb(255, 0xff, 0x80, 0x00), "Orange"),
        new(Color.FromArgb(255, 0xff, 0xff, 0x00), "Gelb"),
    ];

    /// <summary>The highlighters' colors (Xournal++'s usual ones: yellow first).</summary>
    public static readonly NamedColor[] HighlighterColors =
    [
        new(Color.FromArgb(255, 0xff, 0xff, 0x00), "Gelb"),
        new(Color.FromArgb(255, 0x00, 0xff, 0x00), "Grün"),
        new(Color.FromArgb(255, 0x00, 0xc0, 0xff), "Hellblau"),
        new(Color.FromArgb(255, 0xff, 0x00, 0xff), "Magenta"),
        new(Color.FromArgb(255, 0xff, 0x80, 0x00), "Orange"),
    ];

    public static PenToolbar Create(AppSettings settings)
    {
        // Xournal's medium width; black, blue, red from its palette. Highlighters: yellow and green.
        var pens = settings.Pens.Count > 0
            ? settings.Pens.Select(p => new PenPreset(p.ColorIndex, p.Width))
            : new[] { 0, 1, 2 }.Select(color => new PenPreset(color, settings.PenStrokeWidth));
        var highlighters = settings.Highlighters.Count > 0
            ? settings.Highlighters.Select(p => new PenPreset(p.ColorIndex, p.Width))
            : new[] { 0, 1 }.Select(color => new PenPreset(color, settings.HighlighterStrokeWidth));
        return new PenToolbar(German, PenColors, pens.ToList(), HighlighterColors, highlighters.ToList());
    }
}
