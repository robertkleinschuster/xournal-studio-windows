#pragma once

// Exporting a document: the one call the app makes ("export this document to that file"), in two steps because the
// pages' ink can only be read on the ink thread while writing the PDF takes long:
//   auto source = pdf::PrepareExport(doc);                 // ink/UI thread: snapshot + resolved files
//   auto result = pdf::ExportDocument(source, target);     // any thread
// Mirrors the C# PdfExport.Write: backgrounds (the document's PDF, color + ruling, picture), then each layer's elements
// in file order - strokes (as Xournal draws them), pictures, texts; anything else is counted as skipped.

#include "xournal/model/XoppDocument.h"
#include "xournal/pdf/PdfExport.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xournal::pdf
{
    struct DocumentExportSource
    {
        XmlElementPtr snapshot;                                       // the <xournal> element as it would be saved
        std::optional<std::filesystem::path> backgroundPdf;           // the document's PDF (it may be missing)
        std::vector<std::optional<std::filesystem::path>> pictureBackgrounds; // per page: its picture background's file
    };

    /// Takes the snapshot and the files the pages show (call it where the document's ink may be read).
    DocumentExportSource PrepareExport(XoppDocument& doc);

    /// The pages as the export draws them; `result` gets the skipped elements and the pages whose background file is
    /// unknown.
    std::vector<ExportPage> PagesOf(DocumentExportSource const& source, ExportResult& result);

    /// Writes the document as PDF to `target` - never over its background PDF (throws). Any thread.
    ExportResult ExportDocument(DocumentExportSource const& source, std::filesystem::path const& target, ExportOptions const& options = {});
}
