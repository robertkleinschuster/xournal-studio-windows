#pragma once

// Following places of the background PDF: an outline entry or an internal link goes to the document page that shows
// its PDF page (nearest to the page in view) and the place on it; a finger tap or mouse click on a link follows it
// (InkPages' `tapped` - the pen never taps); a web link (URI) opens in the default program after a question that shows
// the address. Links are read per PDF page from the PDF's navigation once and kept while the PDF stays.

#include "DocumentView.h"

#include "inkpages/WritingSurface.h"
#include "xournal/pdf/PdfNavigation.h"

#include <windows.h>

#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xournal::app
{
    class PdfLinks
    {
    public:
        PdfLinks(HWND owner, inkpages::WritingSurface& surface, DocumentView& view);

        /// Goes to a place of the background PDF (an outline entry, an internal link); false when no page shows it.
        bool Go(pdf::Destination const& destination);

        /// A tap on a page (UI thread): follows the link there, if any.
        void Tapped(int page, inkpages::PointF point);

        /// The link at a place of a page (page coordinates), if any.
        std::optional<pdf::Link> LinkAt(int page, inkpages::PointF point);
        /// What a link leads to, for its tooltip: the web address, or "Seite n" (the document page that shows its target).
        std::wstring Describe(pdf::Link const& link);
        /// Before Go moves the view (a link, an outline entry): Back returns to where the view was.
        std::function<void()> jumping;

    private:
        std::vector<pdf::Link> const& LinksOf(pdf::PdfNavigation const& navigation, int pdfPage);
        void OpenUri(std::wstring const& uri);

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
        pdf::PdfNavigation const* cachedFor = nullptr;         // the navigation `links` belong to
        std::shared_ptr<pdf::PdfNavigation const> keep;         // ... kept alive with them
        std::map<int, std::vector<pdf::Link>> links;            // by PDF page
    };
}
