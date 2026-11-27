#pragma once

// Datei -> Für iPad exportieren: the counterpart of the iPad import. The document as a PDF that the iPad's markup can
// edit: the background PDF's pages kept exactly, the pen strokes written as Apple's ink annotations (one /Stamp per page
// with a PencilKit drawing in /AAPL:AKExtras, the archive iPadOS writes - see AppleMarkup::WriteInk) with a vector
// appearance, so every PDF viewer shows them too. Only what the iPad can edit goes out (user, 2026-11-27): whatever has no
// counterpart there is LEFT OUT, never drawn into the page as something uneditable, and the app warns before it exports a
// document that has such content (OmittedByIpadExport). Format facts: CLAUDE.md, "iPad export".

#include "xournal/pdf/DocumentExport.h"

#include <filesystem>

namespace xournal::import
{
    /// What the export leaves out because the iPad has no counterpart for it yet (counted, never drawn into the page).
    struct IpadOmitted
    {
        int pictures = 0;       // not yet (open work)
        int other = 0;          // elements the app does not read at all
        int Total() const { return pictures + other; }
    };

    struct IpadExportResult
    {
        pdf::ExportResult pdf;   // the pages, what the PDF export left out
        int drawings = 0;        // pages that got an ink annotation
        int strokes = 0;         // pen strokes written as ink
        int elements = 0;        // strokes, texts... written as standard annotations (/Ink, /FreeText)
        IpadOmitted omitted;     // what is not in the file
    };

    /// What ExportForIpad would leave out of the document, counted without writing (for the warning before the export).
    IpadOmitted OmittedByIpadExport(pdf::DocumentExportSource const& source);

    /// Writes `target` ("<target>~" first, then replacing it). Never over the document's own PDF (throws). Any thread.
    IpadExportResult ExportForIpad(pdf::DocumentExportSource const& source, std::filesystem::path const& target,
                                   pdf::ExportOptions const& options = {});
}
