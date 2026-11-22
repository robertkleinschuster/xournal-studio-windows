#pragma once

// The document's background PDF on screen: loaded from memory on the render thread (the file stays free), its pages
// rendered on the render thread, nearest to the page in view first, in two parts per page:
//   - a preview: the whole page, at most PreviewWidth pixels wide - quick to render; drawn scaled while the sharp tiles
//     are on their way (a page scrolled into view, a new zoom);
//   - sharp tiles (pdf::TileGrid) at exactly the sheet's pixels per unit, on its pixel grid (PDF_RENDER_PARAMS
//     SourceRect + destination size): drawn 1:1 at any zoom. Up to WholePageMax pixels the page is one tile; beyond,
//     2048 px tiles of the area the sheet draws (InkPages draws the sheet near the view only). Each is rendered once per
//     zoom and kept while the view scrolls over it; until a new zoom's tiles are there, the last zoom's stay as the
//     placeholder.
// Memory copies are let go once uploaded; tiles outside the drawn area and pages far from the view give their bitmaps
// back. The PDF's navigation (outline, links: pdf::PdfNavigation) is read from the same bytes on the render thread
// (40-50 ms for 500 pages) for the outline pane and links.
//
// Threads: render jobs share only the loaded PDF (Source: no UI objects); this object and its Direct2D bitmaps live on
// the UI thread and are reached from a job only through a weak pointer in the work it posts back - so a job never owns
// (and never destroys) anything of the UI thread.

#include "Bitmaps.h"
#include "DarkPdf.h"
#include "RenderQueue.h"

#include "inkpages/model/PageMap.h"
#include "xournal/model/Geometry.h"
#include "xournal/pdf/PdfNavigation.h"
#include "xournal/pdf/RenderTiles.h"

#include <d2d1_1.h>

#include <atomic>
#include <filesystem>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <tuple>

namespace xournal::app
{
    class PdfBackground : public std::enable_shared_from_this<PdfBackground>
    {
    public:
        /// Bitmap arrivals on the UI thread, grouped for one redraw.
        PdfBackground(RenderQueue& queue, std::uint64_t generation, std::function<void(std::set<int> const&)> ready);

        /// Starts loading the PDF (render thread).
        void Load(std::filesystem::path const& pdf);

        /// Draws PDF page `pdfPage` over `size` (page units) for document page `page` where `area` (page units) is drawn
        /// at `pixelsPerUnit` (the sheet's pixel grid: unit * pixelsPerUnit = pixel); asks for what is missing (UI thread).
        void Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, D2D1_RECT_F area, float pixelsPerUnit, bool dark);

        /// A PDF page rendered once at `width` x `height` pixels for a thumbnail (render thread, near `priorityPage` first;
        /// a newer request for the same PDF page replaces a waiting one). `done` runs on the UI thread, with nothing when
        /// the page cannot be rendered (logged) or the PDF is not loaded.
        void RenderPage(std::uint64_t key, int pdfPage, UINT width, UINT height, int priorityPage, bool dark,
            std::function<void(std::shared_ptr<CpuBitmap>)> done);
        /// Explicit copy of an area from the loaded PDF. Completion runs on the render thread.
        void RenderArea(int pdfPage, int documentPage, Rect area, double scale, bool transparent,
            std::function<void(std::shared_ptr<CpuBitmap>, std::exception_ptr)> done);

        /// Pages were inserted, removed or moved: the document pages showing each PDF page follow.
        void Remap(inkpages::PageMap const& map);

        /// Lets go of previews of document pages farther than `keep` from `focus`, and of the tiles of pages not next to
        /// it (UI thread).
        void Trim(int focus, int keep, std::vector<int> const& drawing);

        /// The PDF's outline and links once loaded (nothing before, or when they could not be read).
        std::shared_ptr<pdf::PdfNavigation const> Navigation() const;
        std::shared_ptr<std::vector<std::uint8_t> const> Data() const;

        /// Whether the PDF is loaded (thumbnails asked before are asked again then).
        bool Loaded() const;
        /// Runs on the UI thread once the PDF and its navigation are loaded (the outline can be shown then).
        std::function<void()> loaded;

        /// A PDF page's shown size in points (crop box turned by /Rotate, as rendered); nothing before the PDF is loaded
        /// or for a page it does not have (UI thread).
        std::optional<Size> ShownSize(int pdfPage);

        /// Whether the PDF could not be loaded (the pages stay white; the reason is in the log).
        bool Failed() const { return source->failed; }

    private:
        using PageKey = std::tuple<int, float, float>; // PDF page, document page width and height
        // What the render thread needs: the loaded PDF (agile) and its navigation - nothing of the UI thread.
        struct Source
        {
            mutable std::mutex lock;
            winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
            std::shared_ptr<pdf::PdfNavigation const> navigation;
            std::shared_ptr<std::vector<std::uint8_t> const> data;
            std::atomic<bool> failed = false;

            winrt::Windows::Data::Pdf::PdfDocument Document() const;
        };

        // The sharp tiles of a page at one zoom: rendered ones and those on their way, by index in the grid.
        struct Tiles
        {
            float pixelsPerUnit = 0;
            std::map<int, std::unique_ptr<DrawableBitmap>> done;
            std::map<int, std::uint64_t> asked;
        };

        struct Rendered
        {
            std::unique_ptr<DrawableBitmap> preview;
            UINT previewWidth = 0;
            std::uint64_t previewRequest = 0;
            Tiles tiles;                  // at the zoom drawn last
            Tiles placeholder;            // the zoom before, until the new zoom's tiles are there
            bool failed = false;          // the page could not be rendered (logged; not asked again)
            std::set<int> pages;          // document pages showing this PDF page
        };

        void RequestPreview(int page, PageKey key, PixelSize size);
        void RequestTile(int page, PageKey key, float pixelsPerUnit, int index, pdf::PixelRect pixels, int rank);
        void DropTiles(Tiles& tiles, std::set<int> const& indices);
        void DropTiles(Tiles& tiles);
        void DrawTiles(ID2D1DeviceContext* dc, Tiles& tiles, D2D1_SIZE_F size, bool shrunk, D2D1_RECT_F area, bool dark);
        std::uint64_t NextRequest();
        /// Draws the pages again once per batch of arrived renders (UI thread).
        void Ready(PageKey const& key);

        DarkPdf::Drawing darkDrawing;
        RenderQueue& queue;
        std::uint64_t generation;
        std::function<void(std::set<int> const&)> ready;
        std::shared_ptr<Source> source = std::make_shared<Source>();
        std::map<PageKey, Rendered> rendered; // UI thread
        std::map<int, Size> shownSizes;   // by PDF page, read once (UI thread)
        std::set<int> readyPages;         // pages with renders arrived since the last redraw
        bool readyPosted = false;
    };
}
