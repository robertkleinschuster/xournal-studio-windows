#pragma once

// The document's background PDF on screen: loaded from memory on the render thread (the file stays free), its pages
// rendered at the zoom the underlay draws them (RenderScale) on the render thread, nearest to the page in view first.
// Until a page's bitmap is there (or while a sharper one renders) it shows white or the last bitmap scaled. Pages far
// from the view give their bitmaps back. The PDF's navigation (outline, links: pdf::PdfNavigation) is read from the same
// bytes on the render thread (40-50 ms for 500 pages) for the outline pane and links to come.
//
// Threads: render jobs share only the loaded PDF (Source: no UI objects); this object and its Direct2D bitmaps live on
// the UI thread and are reached from a job only through a weak pointer in the work it posts back - so a job never owns
// (and never destroys) anything of the UI thread.

#include "Bitmaps.h"
#include "RenderQueue.h"

#include "inkpages/model/PageMap.h"
#include "xournal/pdf/PdfNavigation.h"

#include <d2d1_1.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

namespace xournal::app
{
    class PdfBackground : public std::enable_shared_from_this<PdfBackground>
    {
    public:
        /// `ready(page)`: a page's bitmap arrived (UI thread) - the underlay of that document page is drawn again.
        PdfBackground(RenderQueue& queue, std::uint64_t generation, std::function<void(int page)> ready);

        /// Starts loading the PDF (render thread).
        void Load(std::filesystem::path const& pdf);

        /// Draws PDF page `pdfPage` over `size` (page units) for document page `page`; asks for a sharper bitmap when
        /// `pixelsPerUnit` needs one (UI thread).
        void Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, float pixelsPerUnit);

        /// A PDF page rendered once at `width` x `height` pixels for a thumbnail (render thread, near `priorityPage` first;
        /// a newer request for the same PDF page replaces a waiting one). `done` runs on the UI thread, with nothing when
        /// the page cannot be rendered (logged) or the PDF is not loaded.
        void RenderPage(int pdfPage, UINT width, UINT height, int priorityPage,
            std::function<void(std::shared_ptr<CpuBitmap>)> done);

        /// Pages were inserted, removed or moved: the document pages showing each PDF page follow.
        void Remap(inkpages::PageMap const& map);

        /// Lets go of bitmaps of document pages farther than `keep` from `focus` (UI thread).
        void Trim(int focus, int keep);

        /// The PDF's outline and links once loaded (nothing before, or when they could not be read).
        std::shared_ptr<pdf::PdfNavigation const> Navigation() const;

        /// Whether the PDF is loaded (thumbnails asked before are asked again then).
        bool Loaded() const;

        /// Whether the PDF could not be loaded (the pages stay white; the reason is in the log).
        bool Failed() const { return source->failed; }

    private:
        // What the render thread needs: the loaded PDF (agile) and its navigation - nothing of the UI thread.
        struct Source
        {
            mutable std::mutex lock;
            winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
            std::shared_ptr<pdf::PdfNavigation const> navigation;
            std::atomic<bool> failed = false;

            winrt::Windows::Data::Pdf::PdfDocument Document() const;
        };

        struct Rendered
        {
            std::unique_ptr<DrawableBitmap> bitmap;
            UINT width = 0;
            UINT requested = 0;      // width of the render on its way (0: none)
            bool failed = false;     // the page could not be rendered (logged; not asked again)
            std::set<int> pages;     // document pages showing this PDF page
        };

        void Request(int page, int pdfPage, PixelSize size);

        RenderQueue& queue;
        std::uint64_t generation;
        std::function<void(int)> ready;
        std::shared_ptr<Source> source = std::make_shared<Source>();
        std::map<int, Rendered> rendered; // by PDF page (UI thread)
    };
}
