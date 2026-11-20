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

        // Segoe Fluent Icons for the commands (E74E Save, EDE1 Export).
        constexpr wchar_t GlyphSave = L'\uE74E';
        constexpr wchar_t GlyphExport = L'\uEDE1';

        // The page buttons: a sheet with a folded corner and a plus / minus badge, drawn as paths in Segoe Fluent's style
        // (the C# app's AppToolbar icons; Segoe Fluent Icons documents no "page add/remove" glyph). Nonzero fill: outline,
        // fold and badge clockwise, the plus/minus counter-clockwise (a hole in the badge).
        constexpr wchar_t const* Sheet = L"F1 M5,9.3 V3.5 L5.6,2.1 L7,1.5 H12 L17,6.5 V17 L16.4,18.4 L15,19 H10.7 V17.6 H15 "
            L"L15.4,17.4 L15.6,17 V7.1 L11.4,2.9 H7 L6.6,3.1 L6.4,3.5 V9.3 Z M10.9,2.2 H12.3 V5.5 H16.3 V6.9 H10.9 Z "
            L"M5,10.5 A4.5,4.5 0 1 1 5,19.5 A4.5,4.5 0 1 1 5,10.5 Z ";
        std::wstring const InsertPageIcon = std::wstring(Sheet) + L"M4.3,12.25 V14.3 H2.25 V15.7 H4.3 V17.75 H5.7 V15.7 H7.75 V14.3 H5.7 V12.25 Z";
        std::wstring const DeletePageIcon = std::wstring(Sheet) + L"M2.75,14.3 V15.7 H7.25 V14.3 Z";
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
        t.text = L"Text";
        t.textTip = L"Text: auf die Seite tippen und schreiben (die Tastatur tippt), auf einen Text tippen, um ihn zu "
            L"bearbeiten, daneben tippen, um fertig zu sein. Der Pfeil: Schrift, Größe und Farbe";
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
            // Save and Export at the toolbar's start, where apps put them (user, 2026-11-20).
            { CmdSave, GlyphSave, L"Speichern (Strg+S)", actions.save, L"Speichern", {}, 20, false, true },
            { CmdExportPdf, GlyphExport, L"Als PDF exportieren (Strg+E)", actions.exportPdf, L"Als PDF exportieren", {}, 20, false, true },
            { CmdInsertPage, 0, L"Seite einfügen (nach der Seite im Blick)", actions.insertPage, L"Seite einfügen", InsertPageIcon, 20, true },
            { CmdDeletePage, 0, L"Seite löschen", actions.deletePage, L"Seite löschen", DeletePageIcon, 20 },
        };
        return inkpages::Toolbar::Create(parent, surface, std::move(choices), GermanToolbarTexts(), CursorFor, std::move(buttons));
    }
}
