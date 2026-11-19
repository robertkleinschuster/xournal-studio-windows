#pragma once

// Datei -> PDF mit iPad-Anmerkungen importieren: turns a PDF annotated on the iPad (Apple's markup) into a PDF without
// that markup and the .xopp elements for it - handwriting (PencilKit drawings in Apple's AKInkAnnotation2) becomes
// strokes, text boxes (/FreeText) become texts, every other annotation that shows something (pasted screenshots, stamps,
// signatures) becomes a picture of its appearance; links, form fields and annotations without appearance stay in the
// PDF. Nothing shown is lost: a drawing that cannot be read becomes a picture too. The source PDF is never changed.
// Port of the C# PdfMarkupImport (PDFsharp) on qpdf. Format facts: CLAUDE.md, "iPad import".
//
// Steps (no UI; any thread except where noted):
//   auto import = Prepare(source, stagedPdf, picturesPdf);  // the PDF without the markup + the pictures PDFs
//   RenderPictures(import);                                 // the pictures' data (Windows.Data.Pdf; not on an STA)
//   auto doc = CreateDocument(import, pdf);                 // the .xopp document on `pdf`

#include "xournal/model/Geometry.h"
#include "xournal/model/XoppDocument.h"
#include "xournal/model/Xml.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace xournal::import
{
    /// A picture taken from an annotation's appearance, to be rendered into `element`.
    struct MarkupPicture
    {
        std::filesystem::path pdf; // the pictures PDF it is rendered from (only picture annotations that do not overlap)
        int page = 0;              // the page there (the same index as in the source)
        Rect area;                 // the annotation's area on its page (points, top left; as Windows.Data.Pdf shows it)
        XmlElementPtr element;     // the <image> in place (its data still missing)
    };

    struct MarkupImport
    {
        std::vector<Size> pageSizes;
        std::vector<std::vector<XmlElementPtr>> elements; // per page, in the annotations' order
        std::vector<MarkupPicture> pictures;
        int drawings = 0, texts = 0;
        int kept = 0; // markup annotations without appearance left in the PDF
    };

    /// Writes `targetPdf` (the source without the taken annotations) and, when pictures are needed, the pictures PDFs
    /// picturesPdf(0), (1), ... (the source's pages with only those annotations; a further one only where pictures
    /// overlap). Files are written as "<file>~", then replaced. Throws xournal::Error.
    MarkupImport Prepare(std::filesystem::path const& source, std::filesystem::path const& targetPdf,
                         std::function<std::filesystem::path(int)> const& picturesPdf);

    /// Renders every picture from its pictures PDF (transparent, `pixelsPerPoint`) into its <image>. Uses
    /// Windows.Data.Pdf (blocking): call it off the UI thread.
    void RenderPictures(MarkupImport& import, double pixelsPerPoint = 2);

    /// The .xopp document: one page per PDF page on `pdf`, the import's elements on them (a picture without data - not
    /// rendered - is left out).
    std::unique_ptr<XoppDocument> CreateDocument(MarkupImport const& import, std::filesystem::path const& pdf);
}
