using InkPages;
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
        Eraser = "Radierer",
        PartialEraser = "Teilweise radieren",
        PartialEraserTip = "Teilweise radieren: der Radierer (auch das Radierer-Ende des Stifts) löscht genau den Kreis am Stift. " +
            "Größe: rechte Maustaste oder gedrückt halten.",
        EraserRadius = "Radius (Punkt)",
        Select = "Auswählen",
        SelectTip = "Auswählen: Lasso oder Rechteck (nochmal tippen zum Wählen); ziehen verschiebt, Ecken skalieren; " +
            "ein Rechteck auf einer PDF-Seite kopiert diesen Bereich",
        SelectWith = "Auswählen mit",
        Lasso = "Lasso",
        Rectangle = "Rechteck",
        Shapes = "Formen",
        ShapesTip = "Formen: Linie, Pfeil, Rechteck, Ellipse (nochmal tippen zum Wählen)",
        Shape = "Form",
        ShapeKinds = new[] { "Linie", "Pfeil", "Rechteck", "Ellipse" },
        Ruler = "Lineal / Geodreieck",
        RecognizeShapes = "Formerkennung",
        Undo = "Rückgängig (Strg+Z)",
        Redo = "Wiederholen (Strg+Y)",
        Cut = "Auswahl ausschneiden (Strg+X)",
        Copy = "Auswahl kopieren (Strg+C)",
        Duplicate = "Auswahl duplizieren (Strg+D)",
        DeleteSelection = "Auswahl löschen (Entf)",
    };

    private static readonly string[] PenNames = ["Stift Schwarz", "Stift Blau", "Stift Rot"];

    public static PenToolbar Create(AppSettings settings)
    {
        // Xournal's medium width; black, blue, red from its palette.
        var pens = settings.Pens.Count > 0
            ? settings.Pens.Select((p, i) => new PenPreset(i < PenNames.Length ? PenNames[i] : $"Stift {i + 1}", p.ColorIndex, p.Width))
            : new[] { 0, 1, 2 }.Select((color, i) => new PenPreset(PenNames[i], color, settings.PenStrokeWidth));
        return new PenToolbar(German, ToolController.PenColors, pens.ToList());
    }
}
