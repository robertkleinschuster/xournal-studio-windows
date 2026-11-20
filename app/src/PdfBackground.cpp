#include "PdfBackground.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr UINT PreviewWidth = 640; // the placeholder: ~15 ms to render, 2.3 MB for A4
        // Tiles: a render call costs ~10 ms on top of ~3 ms per million pixels (Intel Iris Xe, measured): a page of up to
        // 8 Mpx (A4 up to ~2.6x zoom at 150 %) is one tile (one call, kept while scrolling over the page); a larger one
        // is cut into 2048 px tiles, so only the area the sheet draws is rendered and held (A4 at 6x on 150 %: 72 Mpx).
        constexpr long long WholePageMax = 8'000'000;
        constexpr int TileSize = 2048;
        constexpr int SharpAround = 1;     // tiles stay for the page in view and its neighbours

        // Render keys: the generation (document) in the high bits; below, a preview's: the PDF page; a tile's: bit 28,
        // the PDF page (14 bits) and the tile's index (14 bits). Thumbnails use bits 29/30, pictures bit 31.
        constexpr std::uint32_t TileBit = 0x10000000u;
        std::uint64_t KeyOf(std::uint64_t generation, int pdfPage) { return (generation << 32) | static_cast<std::uint32_t>(pdfPage); }
        std::uint64_t TileKey(std::uint64_t generation, int pdfPage, int index)
        {
            return (generation << 32) | TileBit | (static_cast<std::uint32_t>(pdfPage & 0x3FFF) << 14) | static_cast<std::uint32_t>(index & 0x3FFF);
        }

        // Draws `bitmap` into `dest` (page units). Its memory copy may be gone and the device changed since: then the
        // bitmap is let go (false) and rendered again. 1:1 on the pixel grid: plain sampling; shrunk (a placeholder of
        // a higher zoom): high-quality cubic, which prefilters (no aliasing of fine lines); enlarged: linear.
        bool DrawPart(ID2D1DeviceContext* dc, std::unique_ptr<DrawableBitmap>& bitmap, D2D1_RECT_F dest, bool shrunk)
        {
            if (!bitmap) return false;
            ID2D1Bitmap* uploaded = bitmap->On(dc);
            if (!uploaded)
            {
                bitmap.reset();
                return false;
            }
            dc->DrawBitmap(uploaded, dest, 1.0f, shrunk ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR);
            return true;
        }

        pdf::TileGrid GridOf(D2D1_SIZE_F size, float pixelsPerUnit)
        {
            int width = static_cast<int>(std::ceil(size.width * pixelsPerUnit)), height = static_cast<int>(std::ceil(size.height * pixelsPerUnit));
            bool whole = static_cast<long long>(width) * height <= WholePageMax;
            return { width, height, whole ? std::max(width, height) : TileSize };
        }
    }

    winrt::Windows::Data::Pdf::PdfDocument PdfBackground::Source::Document() const
    {
        std::scoped_lock guard(lock);
        return document;
    }

    PdfBackground::PdfBackground(RenderQueue& queue, std::uint64_t generation, std::function<void(int)> ready)
        : queue(queue), generation(generation), ready(std::move(ready))
    {
    }

    void PdfBackground::Load(std::filesystem::path const& pdf)
    {
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(KeyOf(generation, -1), -1000000, "load background PDF", [weak, source = source, pdf](PdfPageRenderer&)
        {
            try
            {
                auto data = io::Files::ReadAll(pdf);
                auto document = Bitmaps::LoadPdf(data);
                {
                    std::scoped_lock guard(source->lock);
                    source->document = document;
                }
                Log(std::format("pdf background: {} loaded, {} pages", ToUtf8(pdf.native()), document.PageCount()));
                // The navigation from the same bytes; a PDF whose outline qpdf cannot read still shows its pages.
                try
                {
                    auto start = std::chrono::steady_clock::now();
                    auto navigation = std::make_shared<pdf::PdfNavigation const>(data);
                    auto outline = navigation->Outline();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                    std::scoped_lock guard(source->lock);
                    source->navigation = std::move(navigation);
                    Log(std::format("pdf background: navigation read in {} ms ({} outline entries at the top)", ms, outline.size()));
                }
                catch (...)
                {
                    LogException(std::format("pdf background: navigation of {}", ToUtf8(pdf.native())));
                }
            }
            catch (...)
            {
                source->failed = true;
                LogException(std::format("pdf background: loading {}", ToUtf8(pdf.native())));
                return;
            }
            // Pages that asked before the PDF was there ask again when they are drawn.
            UiQueue::Post("pdf background loaded", [weak]
            {
                if (auto self = weak.lock())
                {
                    for (auto const& [pdfPage, entry] : self->rendered)
                        for (int page : entry.pages) self->ready(page);
                    if (self->loaded) self->loaded();
                }
            });
        });
    }

    bool PdfBackground::Loaded() const
    {
        return source->Document() != nullptr;
    }

    std::optional<Size> PdfBackground::ShownSize(int pdfPage)
    {
        if (auto it = shownSizes.find(pdfPage); it != shownSizes.end()) return it->second;
        auto document = source->Document();
        if (!document || pdfPage < 0 || static_cast<UINT>(pdfPage) >= document.PageCount()) return std::nullopt;
        auto dips = document.GetPage(static_cast<UINT>(pdfPage)).Size(); // DIPs: points x 96/72
        Size shown{ dips.Width * 72.0 / 96.0, dips.Height * 72.0 / 96.0 };
        shownSizes[pdfPage] = shown;
        return shown;
    }

    std::shared_ptr<pdf::PdfNavigation const> PdfBackground::Navigation() const
    {
        std::scoped_lock guard(source->lock);
        return source->navigation;
    }

    void PdfBackground::Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, D2D1_RECT_F area, float pixelsPerUnit)
    {
        Rendered& entry = rendered[pdfPage];
        entry.pages.insert(page);
        if (!(pixelsPerUnit > 0) || !(size.width > 0) || !(size.height > 0)) return;
        float s = pixelsPerUnit;
        bool asking = !entry.failed && !source->failed;

        // The preview, scaled over the whole page.
        DrawPart(dc, entry.preview, { 0, 0, size.width, size.height }, entry.previewWidth > size.width * s);
        PixelSize preview = FitPixels(size.width, size.height, s, PreviewWidth);
        if (asking && entry.previewWidth != preview.width && entry.previewRequested != preview.width)
        {
            RequestPreview(page, pdfPage, preview);
        }

        // A new zoom: the last zoom's tiles become the placeholder, their waiting renders are dropped.
        if (entry.tiles.pixelsPerUnit != s)
        {
            DropTiles(pdfPage, entry.tiles.asked);
            entry.tiles.asked.clear();
            if (!entry.tiles.done.empty()) entry.placeholder = std::move(entry.tiles);
            entry.tiles = Tiles{ s };
        }
        if (!entry.placeholder.done.empty() && entry.placeholder.pixelsPerUnit * size.width >= entry.previewWidth)
        {
            DrawTiles(dc, entry.placeholder, size, entry.placeholder.pixelsPerUnit > s);
        }

        // The tiles of the drawn area; those it left give their bitmaps back.
        pdf::TileGrid grid = GridOf(size, s);
        pdf::PixelRect pixels{ static_cast<int>(std::floor(area.left * s)), static_cast<int>(std::floor(area.top * s)),
            static_cast<int>(std::ceil(area.right * s)), static_cast<int>(std::ceil(area.bottom * s)) };
        std::vector<int> needed = grid.Covering(pixels);
        std::set<int> neededSet(needed.begin(), needed.end());
        if (entry.pages.size() == 1) // a PDF page shown on several pages keeps the tiles of all their areas
        {
            std::erase_if(entry.tiles.done, [&](auto const& tile) { return !neededSet.contains(tile.first); });
            std::set<int> left;
            for (int index : entry.tiles.asked)
                if (!neededSet.contains(index)) left.insert(index);
            DropTiles(pdfPage, left);
            for (int index : left) entry.tiles.asked.erase(index);
        }
        DrawTiles(dc, entry.tiles, size, false);

        int missing = 0;
        for (int index : needed)
        {
            if (entry.tiles.done.contains(index)) continue;
            ++missing;
            if (asking && !entry.tiles.asked.contains(index))
            {
                RequestTile(page, pdfPage, size, s, index, grid.Rect(index), missing); // the area's middle first
            }
        }
        if (missing == 0) entry.placeholder = {};
        Log(std::format("pdf background: page {} at {:.3f} px/unit: preview {} of {} px, tiles {} of {} there", pdfPage + 1, s,
            entry.previewWidth, preview.width, needed.size() - missing, needed.size()));
    }

    void PdfBackground::DrawTiles(ID2D1DeviceContext* dc, Tiles& tiles, D2D1_SIZE_F size, bool shrunk)
    {
        float k = tiles.pixelsPerUnit;
        pdf::TileGrid grid = GridOf(size, k);
        for (auto& [index, bitmap] : tiles.done)
        {
            pdf::PixelRect r = grid.Rect(index);
            DrawPart(dc, bitmap, { r.left / k, r.top / k, r.right / k, r.bottom / k }, shrunk);
        }
        std::erase_if(tiles.done, [](auto const& tile) { return !tile.second; }); // let go after a device change: asked again
    }

    void PdfBackground::DropTiles(int pdfPage, std::set<int> const& indices)
    {
        if (indices.empty()) return;
        std::set<std::uint64_t> keys;
        for (int index : indices) keys.insert(TileKey(generation, pdfPage, index));
        queue.Drop([&](std::uint64_t key) { return keys.contains(key); }); // one already running is dropped when it arrives
    }

    void PdfBackground::Ready(int pdfPage)
    {
        auto const& pages = rendered[pdfPage].pages;
        readyPages.insert(pages.begin(), pages.end());
        if (readyPosted) return;
        readyPosted = true;
        std::weak_ptr<PdfBackground> weak = weak_from_this();
        UiQueue::Post("pdf pages ready", [weak]
        {
            auto self = weak.lock();
            if (!self) return;
            self->readyPosted = false;
            auto pages = std::move(self->readyPages);
            self->readyPages.clear();
            for (int page : pages) self->ready(page);
        });
    }

    void PdfBackground::RequestPreview(int page, int pdfPage, PixelSize size)
    {
        rendered[pdfPage].previewRequested = size.width;
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(KeyOf(generation, pdfPage), page, std::format("render PDF page {} preview at {} px", pdfPage + 1, size.width),
            [weak, source = source, pdfPage, size](PdfPageRenderer& renderer)
        {
            auto document = source->Document();
            std::shared_ptr<CpuBitmap> bitmap;
            bool failed = false;
            if (document && static_cast<UINT>(pdfPage) < document.PageCount())
            {
                try
                {
                    bitmap = renderer.Render(document.GetPage(static_cast<UINT>(pdfPage)), size.width, size.height, false);
                }
                catch (...)
                {
                    LogException(std::format("pdf background: page {} at {}x{} px (not asked again)", pdfPage + 1, size.width, size.height));
                    failed = true;
                }
            }
            else if (document)
            {
                LogError(std::format("pdf background: page {} asked, the PDF has {}", pdfPage + 1, document.PageCount()));
                failed = true;
            }
            // Not loaded yet: the request is open again (it is asked once the PDF is there).
            UiQueue::Post("pdf page preview rendered", [weak, pdfPage, size, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                Rendered& entry = self->rendered[pdfPage];
                if (entry.previewRequested == size.width) entry.previewRequested = 0;
                entry.failed = entry.failed || failed;
                if (!bitmap) return;
                entry.preview = std::make_unique<DrawableBitmap>(bitmap, false);
                entry.previewWidth = size.width;
                self->Ready(pdfPage);
            });
        }, -1); // before the page's tiles: the placeholder comes first
    }

    void PdfBackground::RequestTile(int page, int pdfPage, D2D1_SIZE_F size, float pixelsPerUnit, int index, pdf::PixelRect pixels,
        int rank)
    {
        rendered[pdfPage].tiles.asked.insert(index);
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        UINT width = static_cast<UINT>(pixels.Width()), height = static_cast<UINT>(pixels.Height());
        queue.Submit(TileKey(generation, pdfPage, index), page, std::format("render PDF page {} tile {}: {}x{} px at {:.3f} px/unit",
            pdfPage + 1, index, width, height, pixelsPerUnit), [weak, source = source, pdfPage, size, pixelsPerUnit, index, pixels, width,
            height](PdfPageRenderer& renderer)
        {
            auto document = source->Document();
            std::shared_ptr<CpuBitmap> bitmap;
            bool failed = false;
            if (document && static_cast<UINT>(pdfPage) < document.PageCount())
            {
                try
                {
                    // Sheet pixels -> page units -> the PDF page's DIPs (a page may be shown at another size than its
                    // PDF page's: the .xopp keeps the page's own size).
                    auto pdfPageObject = document.GetPage(static_cast<UINT>(pdfPage));
                    auto dips = pdfPageObject.Size();
                    float kx = dips.Width / size.width / pixelsPerUnit, ky = dips.Height / size.height / pixelsPerUnit;
                    D2D_RECT_F part{ pixels.left * kx, pixels.top * ky, pixels.right * kx, pixels.bottom * ky };
                    bitmap = renderer.Render(pdfPageObject, width, height, false, part);
                }
                catch (...)
                {
                    LogException(std::format("pdf background: page {}, tile {} (not asked again)", pdfPage + 1, index));
                    failed = true;
                }
            }
            UiQueue::Post("pdf page tile rendered", [weak, pdfPage, pixelsPerUnit, index, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                Rendered& entry = self->rendered[pdfPage];
                entry.failed = entry.failed || failed;
                // Another zoom meanwhile, or the tile left the drawn area: not needed. Not loaded yet: asked again
                // when the PDF is there.
                if (entry.tiles.pixelsPerUnit != pixelsPerUnit || !entry.tiles.asked.erase(index) || !bitmap) return;
                entry.tiles.done[index] = std::make_unique<DrawableBitmap>(bitmap, false);
                self->Ready(pdfPage);
            });
        }, rank);
    }

    void PdfBackground::RenderPage(int pdfPage, UINT width, UINT height, int priorityPage,
        std::function<void(std::shared_ptr<CpuBitmap>)> done)
    {
        // Thumbnail keys beside the screen's PDF pages and pictures: bit 30 set.
        std::uint64_t key = (generation << 32) | 0x40000000u | static_cast<std::uint32_t>(pdfPage);
        queue.Submit(key, priorityPage, std::format("render PDF page {} for a thumbnail", pdfPage + 1),
            [source = source, pdfPage, width, height, done](PdfPageRenderer& renderer)
        {
            std::shared_ptr<CpuBitmap> bitmap;
            auto document = source->Document();
            if (document && static_cast<UINT>(pdfPage) < document.PageCount())
            {
                try
                {
                    bitmap = renderer.Render(document.GetPage(static_cast<UINT>(pdfPage)), width, height, false);
                }
                catch (...)
                {
                    LogException(std::format("pdf background: thumbnail of page {}", pdfPage + 1));
                }
            }
            UiQueue::Post("pdf thumbnail rendered", [done, bitmap] { done(bitmap); });
        });
    }

    void PdfBackground::Remap(inkpages::PageMap const& map)
    {
        for (auto& [pdfPage, entry] : rendered)
        {
            entry.pages = map.Apply(entry.pages);
        }
    }

    void PdfBackground::Trim(int focus, int keep)
    {
        queue.SetFocus(focus);
        for (auto& [pdfPage, entry] : rendered)
        {
            auto within = [&](int distance)
            {
                return std::any_of(entry.pages.begin(), entry.pages.end(), [&](int page) { return std::abs(page - focus) <= distance; });
            };
            // A render that has not started when its page left the view is dropped.
            if (!within(keep) && (entry.preview || entry.previewRequested))
            {
                entry.preview.reset();
                entry.previewWidth = 0;
                entry.previewRequested = 0;
                std::uint64_t key = KeyOf(generation, pdfPage);
                queue.Drop([key](std::uint64_t k) { return k == key; });
            }
            if (!within(SharpAround) && (!entry.tiles.done.empty() || !entry.tiles.asked.empty() || !entry.placeholder.done.empty()))
            {
                DropTiles(pdfPage, entry.tiles.asked);
                entry.tiles = {};
                entry.placeholder = {};
            }
        }
    }
}
