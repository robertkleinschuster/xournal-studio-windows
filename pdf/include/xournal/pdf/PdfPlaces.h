#pragma once

// Between places of the background PDF (a PDF page and "shown" points, PageFrame.h) and places of the document (a
// document page and its page units): a document page shows a PDF page stretched over its own size, so the two scale by
// page size / shown size. Several document pages may show the same PDF page (a page copied or inserted twice); a PDF
// place goes to the one nearest to the page in view.

#include "xournal/model/Geometry.h"
#include "xournal/model/XoppDocument.h"
#include "xournal/pdf/PdfNavigation.h"

#include <functional>
#include <optional>

namespace xournal::pdf
{
    struct DocumentPlace
    {
        int page = 0;        // document page (0-based)
        Point point;         // page units; the page's top left when the destination names no position
    };

    /// The shown size (points) of a PDF page; nothing when it is unknown (the document page's size is taken then).
    using ShownSizeOf = std::function<std::optional<Size>(int pdfPage)>;

    /// Where a destination of the background PDF lies in the document: the document page showing its PDF page nearest
    /// to `nearPage`, the position scaled onto that page (clamped to it). Nothing when no page shows that PDF page.
    std::optional<DocumentPlace> FindPlace(XoppDocument const& doc, Destination const& destination, int nearPage,
        ShownSizeOf const& shownSize);

    /// A point on document page `page` in the shown coordinates of the PDF page it shows (for hit-testing links);
    /// nothing when the page shows no PDF page.
    std::optional<Point> ToShown(XoppPage const& page, Point point, ShownSizeOf const& shownSize);

    /// A rectangle in shown coordinates of the PDF page document page `page` shows, in that page's units.
    Rect FromShown(XoppPage const& page, Rect area, ShownSizeOf const& shownSize);
}
