#pragma once

// Datei -> Für iPad exportieren: the counterpart of the iPad import. The document as a PDF that the iPad's markup can
// edit: the background PDF's pages kept exactly, the pen strokes written as Apple's ink annotations (one /Stamp per page
// with a PencilKit drawing in /AAPL:AKExtras, the archive iPadOS writes - see AppleMarkup::WriteInk) with a vector
// appearance, so every PDF viewer shows them too. Everything the markup has no counterpart for stays drawn into the page
// (highlighters, dashed or filled strokes, pictures, texts) as in the normal PDF export.
// Format facts: CLAUDE.md, "iPad export".

#include "xournal/pdf/DocumentExport.h"

#include <filesystem>

namespace xournal::import
{
    struct IpadExportResult
    {
        pdf::ExportResult pdf;   // the pages, what the PDF export left out
        int drawings = 0;        // pages that got an ink annotation
        int strokes = 0;         // pen strokes written as ink
        int flatStrokes = 0;     // strokes left drawn in the page (no counterpart in the markup)
    };

    /// Writes `target` ("<target>~" first, then replacing it). Never over the document's own PDF (throws). Any thread.
    IpadExportResult ExportForIpad(pdf::DocumentExportSource const& source, std::filesystem::path const& target,
                                   pdf::ExportOptions const& options = {});
}
