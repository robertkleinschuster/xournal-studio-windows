#pragma once

// A PDF's navigation: its outline (bookmarks) and the links on its pages, read with qpdf from the PDF's bytes (as the
// app loads PDFs: in memory, the file stays free). Open once per document; Outline() on load, Links(page) as pages come
// into view (the pages are indexed once, a page's links are read on demand).
//
// Places are in "shown" coordinates (see PageFrame.h): points, origin at the shown page's top left, y down - the
// crop box turned by /Rotate, as Windows.Data.Pdf renders it.

#include "xournal/model/Geometry.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace xournal::pdf
{
    /// A place in the document: a page and, when the destination names one (XYZ left/top, FitH top, FitV left, FitR
    /// corner, FitBH/FitBV), a position on it. A coordinate the destination leaves open is the page's left or top edge.
    struct Destination
    {
        int page = 0; // 0-based
        std::optional<Point> position;
    };

    struct OutlineEntry
    {
        std::wstring title;
        std::optional<Destination> destination; // nothing for an entry that only groups others
        std::vector<OutlineEntry> children;
    };

    struct Link
    {
        Rect area; // shown coordinates
        std::variant<Destination, std::wstring> target; // a place in this document, or a URI
    };

    class PdfNavigation
    {
    public:
        /// Parses the PDF (bytes are copied). Throws xournal::Error for data qpdf cannot read.
        explicit PdfNavigation(std::span<std::uint8_t const> pdf);
        ~PdfNavigation();
        PdfNavigation(PdfNavigation const&) = delete;
        PdfNavigation& operator=(PdfNavigation const&) = delete;

        int PageCount() const;

        /// The outline as a tree; named destinations (/Dests, /Names) resolved. An entry whose destination cannot be
        /// resolved is logged; it stays (without destination) when it groups others, otherwise it is left out.
        std::vector<OutlineEntry> Outline() const;

        /// The visible link annotations of a page (Hidden/NoView left out) with a target this document can follow:
        /// GoTo (also named) and URI. Others (GoToR, Launch, JavaScript, named actions) and broken ones are logged and
        /// left out.
        std::vector<Link> Links(int pageIndex) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
