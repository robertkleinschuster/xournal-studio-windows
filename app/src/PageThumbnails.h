#pragma once

// The pages pane's thumbnails (the C# PageThumbnails), in two layers per page:
//   - the background PDF's page, rendered once from the PDF already loaded for the screen;
//   - what lies on it - ink, pictures, texts (a page without PDF: its paper too) - drawn by the PDF export for that one
//     page without the background PDF into memory (pdf::ExportToMemory; nothing of the user's notes goes to a file) and
//     rendered, transparent on a PDF page. Drawn again only for pages an edit touched.
// Pages nearest to the page in view first, only while the pane is shown, a moment after the last change. A layer that
// fails is logged and left (not tried again until the page is edited). Kept per page object, so pages inserted, moved
// or deleted keep their images; far pages give their bitmaps back.

#include "DelayedAction.h"
#include "DocumentView.h"
#include "PdfPageRenderer.h"

#include "xournal/model/XoppPage.h"

#include <functional>
#include <map>
#include <memory>
#include <set>

namespace xournal::app
{
    class PageThumbnails
    {
    public:
        PageThumbnails(DocumentView& view, inkpages::WritingSurface& surface);

        /// The box (pixels) a page is fitted into (ThumbnailSize): the layers are rendered at exactly that size, anew
        /// when it changes (another DPI). Nothing is rendered before the box is set.
        void SetBox(SIZE box);

        struct Layers
        {
            std::shared_ptr<CpuBitmap> background; // the PDF page (nothing yet, or not a PDF page)
            std::shared_ptr<CpuBitmap> content;    // what lies on the page (transparent on a PDF page)
            double aspect = 1.414;                 // height / width
        };

        /// The layers of page `index` as far as they are rendered.
        Layers Of(int index) const;

        /// Thumbnails are made only while the pane is shown.
        void SetActive(bool active);
        /// A new document (all thumbnails start over).
        void Reset();
        /// An edit touched these pages (indices now).
        void Edited(std::set<int> const& pages);
        /// The page list changed (inserted, moved, deleted): new pages get drawn.
        void PagesChanged();

        /// A page's layers changed: the pane redraws that page.
        std::function<void(int index)> updated;

    private:
        struct Thumb
        {
            std::shared_ptr<CpuBitmap> background, content;
            bool backgroundTried = false; // rendered, or failed (not tried again)
            bool contentStale = true;
            bool contentRunning = false;
        };

        void Refresh();
        void RequestBackground(int index, XoppPage const& page, std::shared_ptr<XoppPage> const& key);
        void RequestContent(int index, std::shared_ptr<XoppPage> const& page, XmlElementPtr snapshot);
        int IndexOf(XoppPage const* page) const;
        void Trim(int focus);

        DocumentView& view;
        inkpages::WritingSurface& surface;
        SIZE box{};
        bool active = false;
        std::uint64_t generation = 0; // a new document's results are not mixed with the old one's
        std::map<XoppPage const*, Thumb> thumbs;
        std::map<XoppPage const*, std::shared_ptr<XoppPage>> keep; // the pages the map's keys point to
        DelayedAction refresh;
        // Results posted back after this object is gone (the window closed) find it expired and do nothing.
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    };
}
