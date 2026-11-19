#include "AppToolbar.h"

#include "Commands.h"

namespace xournal::app
{
    namespace
    {
        // The exe's pen cursors (app.rc): a dot per palette color x width step.
        UINT CursorFor(inkpages::Tool tool, inkpages::PenPreset const& preset)
        {
            switch (tool)
            {
            case inkpages::Tool::Pen: return 1000 + preset.color * 10 + inkpages::PenWidths::StepOf(preset.width);
            case inkpages::Tool::Highlighter: return 2000 + preset.color * 10 + inkpages::HighlighterWidths::StepOf(preset.width);
            default: return 0; // other tools: Windows' cursor (the exact eraser shows its circle)
            }
        }

        // Segoe Fluent Icons. (The C# app drew a sheet with a +/- badge as a path; InkPages' buttons take glyphs.)
        constexpr wchar_t GlyphInsertPage = L'\uE710';
        constexpr wchar_t GlyphDeletePage = L'\uE74D';
    }

    inkpages::ToolbarTexts GermanToolbarTexts()
    {
        inkpages::ToolbarTexts t;
        t.pen = L"Stift";
        t.highlighter = L"Textmarker";
        t.presetTip = L"nochmal tippen für Farbe und Stärke";
        t.eraser = L"Radierer";
        t.eraserTip = L"Radierer – die Radierer-Taste des Stifts radiert auch, mit jedem Werkzeug";
        t.wholeStrokeErase = L"Ganze Striche radieren";
        t.wholeStrokeEraseTip = L"Ganze Striche radieren: Radierer und Radierer-Taste des Stifts löschen jeden Strich, "
            L"den sie berühren. Aus: nur, was im Kreis liegt. Der Pfeil: Größe des Kreises";
        t.eraserRadius = L"Größe";
        t.select = L"Auswählen";
        t.selectTip = L"Auswählen: Lasso oder Rechteck (der Pfeil); ziehen verschiebt, die Ecken skalieren. Mit dem "
            L"Stift geht das Lasso mit jedem Werkzeug: mit gedrückter Stifttaste ziehen";
        t.lasso = L"Lasso";
        t.rectangle = L"Rechteck";
        t.shapes = L"Formen";
        t.shapesTip = L"Formen: Linie, Pfeil, Rechteck, Ellipse (der Pfeil); gezeichnete Formen können erkannt werden";
        t.shapeKinds = { L"Linie", L"Pfeil", L"Rechteck", L"Ellipse" };
        t.recognizeShapes = L"Gezeichnete Formen erkennen";
        t.ruler = L"Lineal";
        t.protractor = L"Winkelmesser";
        t.rulerTip = L"Lineal / Winkelmesser (der Pfeil): daran entlang gezogene Linien werden gerade";
        t.cut = L"Auswahl ausschneiden (Strg+X)";
        t.copy = L"Auswahl kopieren (Strg+C)";
        t.paste = L"Einfügen (Strg+V)";
        t.duplicate = L"Auswahl duplizieren (Strg+D)";
        t.deleteSelection = L"Auswahl löschen (Entf)";
        t.selectAll = L"Alles auf der Seite auswählen (Strg+A)";
        t.undo = L"Rückgängig (Strg+Z)";
        t.redo = L"Wiederholen (Strg+Y)";
        t.color = L"Farbe";
        t.width = L"Stärke";
        t.widthFormat = L"{} pt";
        t.decimalSeparator = L',';
        t.penColors = { L"Schwarz", L"Blau", L"Rot", L"Grün", L"Grau", L"Hellblau", L"Hellgrün", L"Magenta", L"Orange", L"Gelb" };
        t.highlighterColors = { L"Gelb", L"Grün", L"Hellblau", L"Magenta", L"Orange" };
        return t;
    }

    std::unique_ptr<inkpages::Toolbar> CreateAppToolbar(HWND parent, inkpages::WritingSurface& surface,
        inkpages::ToolChoices choices, AppToolbarActions actions)
    {
        std::vector<inkpages::ToolbarButton> buttons{
            { CmdInsertPage, GlyphInsertPage, L"Seite einfügen (nach der Seite im Blick)", actions.insertPage },
            { CmdDeletePage, GlyphDeletePage, L"Seite löschen", actions.deletePage },
        };
        return inkpages::Toolbar::Create(parent, surface, std::move(choices), GermanToolbarTexts(), CursorFor, std::move(buttons));
    }
}
