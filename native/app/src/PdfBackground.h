#pragma once

// The document's background PDF on screen: loaded from memory on the render thread (the file stays free), its pages
// rendered at the zoom the underlay draws them (RenderScale) on the render thread, nearest to the page in view first.
// Until a page's bitmap is there (or while a sharper one renders) it shows white or the last bitmap scaled. Pages far
// from the view give their bitmaps back.

#include "Bitmaps.h"
#include "RenderQueue.h"

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

        /// Lets go of bitmaps of document pages farther than `keep` from `focus` (UI thread).
        void Trim(int focus, int keep);

        /// Whether the PDF could not be loaded (the pages stay white; the reason is in the log).
        bool Failed() const { return failed; }

    private:
        struct Rendered
        {
            std::unique_ptr<DrawableBitmap> bitmap;
            UINT width = 0;
            UINT requested = 0;      // width of the render on its way (0: none)
            std::set<int> pages;     // document pages showing this PDF page
        };

        void Request(int page, int pdfPage, UINT width, UINT height);

        RenderQueue& queue;
        std::uint64_t generation;
        std::function<void(int)> ready;
        std::mutex documentLock;                                // the loaded document (render thread writes once)
        winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
        std::atomic<bool> failed = false;
        std::map<int, Rendered> rendered;                       // by PDF page (UI thread)
    };
}
