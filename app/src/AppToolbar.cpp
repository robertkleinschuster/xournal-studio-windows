#include "AppToolbar.h"

#include "Commands.h"
#include "inkpages/model/PaperAppearance.h"

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

        UINT DarkCursorFor(inkpages::Tool tool, inkpages::PenPreset const& preset)
        {
            using namespace inkpages;
            bool highlighter = tool == Tool::Highlighter;
            auto original = highlighter ? Palette::Highlighters[preset.color] : Palette::Pens[preset.color];
            return PaperAppearance::DarkToolInk(original, highlighter) == original ? 0 : CursorFor(tool, preset) + 2000;
        }

    }

    inkpages::ToolbarTexts GermanToolbarTexts()
    {
        inkpages::ToolbarTexts t;
        t.pen = L"Stift";
        t.highlighter = L"Textmarker";
        t.presetTip = L"nochmal tippen für Farbe und Stärke";
        t.writeToolTip = L"Schreibt nur mit dem Stift – Maus und Finger blättern weiter.";
        t.eraser = L"Radierer";
        t.eraserTip = L"Radierer – die Radierer-Taste des Stifts radiert bei jedem Werkzeug. "
            L"Über den Pfeil lässt sich die Radierergröße einstellen.";
        t.wholeStrokeErase = L"Ganze Striche radieren";
        t.wholeStrokeEraseTip = L"Ganze Striche radieren: Radierer und Radierer-Taste des Stifts löschen jeden Strich, "
            L"den sie berühren. Aus: Nur innerhalb der Radierer-Kontur radieren.";
        t.eraserRadius = L"Größe";
        t.sizeSteps = { L"Sehr fein", L"Fein", L"Mittel", L"Dick", L"Sehr dick" };
        t.squareEraser = L"Quadratischer Radierer (Xournal++)";
        t.select = L"Auswählen";
        t.selectTip = L"Auswählen: Lasso oder Rechteck (der Pfeil); ziehen verschiebt, die Ecken skalieren. Mit dem "
            L"Stift geht das Lasso mit jedem Werkzeug: mit gedrückter Stifttaste ziehen.\n"
            L"Wählt mit Stift und Maus aus; der Finger tippt zum Auswählen und blättert.";
        t.lasso = L"Lasso";
        t.rectangle = L"Rechteck";
        t.shapes = L"Formen";
        t.shapesTip = L"Formen: Linie, Pfeil, Rechteck, Ellipse (der Pfeil); gezeichnete Formen können erkannt werden";
        t.shapeKinds = { L"Linie", L"Pfeil", L"Rechteck", L"Ellipse" };
        t.recognizeShapes = L"Gezeichnete Formen erkennen";
        t.text = L"Text";
        t.laser = L"Laserpointer";
        t.copyArea = L"PDF-Bereich kopieren";
        t.copyAreaTransparent = L"Transparent";
        t.copyAreaWithBackground = L"Mit PDF-Hintergrund";
        t.textTip = L"Text: auf die Seite tippen und schreiben (die Tastatur tippt), auf einen Text tippen, um ihn zu "
            L"bearbeiten, daneben tippen, um fertig zu sein. Der Pfeil: Schrift, Größe und Farbe.\n"
            L"Tippen oder Klicken setzt einen Text – mit Stift, Maus oder Finger.";
        t.ruler = L"Lineal";
        t.protractor = L"Winkelmesser";
        t.rulerTip = L"Lineal / Winkelmesser (der Pfeil): daran entlang gezogene Linien werden gerade";
        t.cut = L"Auswahl ausschneiden (Strg+X)";
        t.copy = L"Auswahl kopieren (Strg+C)";
        t.paste = L"Einfügen (Strg+V)";
        t.duplicate = L"Auswahl duplizieren (Strg+D)";
        t.deleteSelection = L"Auswahl löschen (Entf)";
        t.undo = L"Rückgängig (Strg+Z)";
        t.redo = L"Wiederholen (Strg+Y)";
        t.color = L"Farbe";
        t.width = L"Stärke";
        t.widthFormat = L"{} pt";
        t.decimalSeparator = L',';
        t.penColors = { L"Schwarz", L"Blau", L"Rot", L"Grün", L"Grau", L"Hellblau", L"Hellgrün", L"Magenta", L"Orange", L"Gelb" };
        t.highlighterColors = { L"Gelb", L"Grün", L"Hellblau", L"Magenta", L"Orange", L"Rot", L"Rosa", L"Lavendel",
            L"Blau", L"Türkis", L"Mint", L"Apricot" };
        return t;
    }

    std::unique_ptr<inkpages::Toolbar> CreateAppToolbar(HWND parent, inkpages::WritingSurface& surface,
        inkpages::ToolChoices choices, AppToolbarActions actions)
    {
        std::vector<inkpages::ToolbarButton> buttons{
            // Save and Export at the toolbar's start, where apps put them (user, 2026-11-20).
            { CmdSave, 0, L"Speichern (Strg+S)", actions.save, L"Speichern", {}, 20, false, true, inkpages::ToolbarSymbol::Save },
            { CmdExportPdf, 0, L"Als PDF exportieren (Strg+E)", actions.exportPdf, L"Als PDF exportieren", {}, 20, false, true, inkpages::ToolbarSymbol::ExportPdf },
            { CmdInsertPage, 0, L"Seite einfügen (nach der Seite im Blick)", actions.insertPage, L"Seite einfügen", {}, 20, true, false, inkpages::ToolbarSymbol::InsertPage },
            { CmdDeletePage, 0, L"Seite löschen", actions.deletePage, L"Seite löschen", {}, 20, false, false, inkpages::ToolbarSymbol::DeletePage },
        };
        return inkpages::Toolbar::Create(parent, surface, std::move(choices), GermanToolbarTexts(), CursorFor,
            std::move(buttons), DarkCursorFor);
    }
}
