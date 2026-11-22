#include "PdfBackground.h"

#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <utility>

namespace xournal::app
{
    namespace
    {
        constexpr UINT PreviewSide = 640;
        // Tiles: a render call costs ~10 ms on top of ~3 ms per million pixels (Intel Iris Xe, measured): a page of up to
        // 8 Mpx (A4 up to ~2.6x zoom at 150 %) is one tile (one call, kept while scrolling over the page); a larger one
        // is cut into 2048 px tiles, so only the area the sheet draws is rendered and held (A4 at 6x on 150 %: 72 Mpx).
        constexpr long long WholePageMax = 8'000'000;
        constexpr int TileSize = 2048;
        constexpr int SharpAround = 1;     // tiles stay for the page in view and its neighbours

        // Draws `bitmap` into `dest` (page units). Its memory copy may be gone and the device changed since: then the
        // bitmap is let go (false) and rendered again. 1:1 on the pixel grid: plain sampling; shrunk (a placeholder of
        // a higher zoom): high-quality cubic, which prefilters (no aliasing of fine lines); enlarged: linear.
        bool DrawPart(ID2D1DeviceContext* dc, std::unique_ptr<DrawableBitmap>& bitmap, D2D1_RECT_F dest, bool shrunk,
            DarkPdf::Drawing* darkDrawing)
        {
            if (!bitmap) return false;
            ID2D1Bitmap* uploaded = bitmap->On(dc);
            if (!uploaded)
            {
                bitmap.reset();
                return false;
            }
            auto interpolation = shrunk ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC : D2D1_INTERPOLATION_MODE_LINEAR;
            if (darkDrawing) darkDrawing->Draw(dc, uploaded, dest, interpolation);
            else dc->DrawBitmap(uploaded, dest, 1.0f, interpolation);
            return true;
        }

        pdf::TileGrid GridOf(D2D1_SIZE_F size, float pixelsPerUnit)
        {
            int width = static_cast<int>(std::ceil(size.width * pixelsPerUnit)), height = static_cast<int>(std::ceil(size.height * pixelsPerUnit));
            bool whole = std::max(width, height) <= 16384 && static_cast<long long>(width) * height <= WholePageMax;
            return { width, height, whole ? std::max(width, height) : TileSize };
        }

        pdf::PixelRect PixelsOf(D2D1_RECT_F area, float scale)
        {
            return { static_cast<int>(std::floor(area.left * scale)), static_cast<int>(std::floor(area.top * scale)),
                static_cast<int>(std::ceil(area.right * scale)), static_cast<int>(std::ceil(area.bottom * scale)) };
        }
    }

    winrt::Windows::Data::Pdf::PdfDocument PdfBackground::Source::Document() const
    {
        std::scoped_lock guard(lock);
        return document;
    }

    PdfBackground::PdfBackground(RenderQueue& queue, std::uint64_t generation, std::function<void(std::set<int> const&)> ready)
        : queue(queue), generation(generation), ready(std::move(ready))
    {
    }

    std::uint64_t PdfBackground::NextRequest()
    {
        // Bit 28 reserves background bitmap requests. Thumbnails and pictures use bits 29–31.
        static std::atomic_uint32_t request = 0;
        return (generation << 32) | 0x10000000u | (++request & 0x0FFFFFFFu);
    }

    void PdfBackground::Load(std::filesystem::path const& pdf)
    {
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(NextRequest(), -1000000, "load background PDF", [weak, source = source, pdf](PdfPageRenderer&)
        {
            if (weak.expired()) return;
            try
            {
                auto data = std::make_shared<std::vector<std::uint8_t> const>(io::Files::ReadAll(pdf));
                auto document = Bitmaps::LoadPdf(*data);
                {
                    std::scoped_lock guard(source->lock);
                    source->document = document;
                    source->data = data;
                }
                Log(std::format("pdf background: {} loaded, {} pages", ToUtf8(pdf.native()), document.PageCount()));
                // The navigation from the same bytes; a PDF whose outline qpdf cannot read still shows its pages.
                try
                {
                    auto start = std::chrono::steady_clock::now();
                    auto navigation = std::make_shared<pdf::PdfNavigation const>(*data);
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
                    std::set<int> pages;
                    for (auto const& [pdfPage, entry] : self->rendered)
                        pages.insert(entry.pages.begin(), entry.pages.end());
                    if (!pages.empty()) self->ready(pages);
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

    std::shared_ptr<std::vector<std::uint8_t> const> PdfBackground::Data() const
    {
        std::scoped_lock guard(source->lock);
        return source->data;
    }

    void PdfBackground::Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, D2D1_RECT_F area, float pixelsPerUnit, bool dark)
    {
        if (!(pixelsPerUnit > 0) || !(size.width > 0) || !(size.height > 0)) return;
        PageKey key{ pdfPage, size.width, size.height };
        Rendered& entry = rendered[key];
        entry.pages.insert(page);
        float s = pixelsPerUnit;
        bool asking = !entry.failed && !source->failed;

        // Reuse the previous grid when returning to its zoom. In-flight work still belongs to its original request.
        if (entry.tiles.pixelsPerUnit != s)
        {
            DropTiles(entry.tiles);
            if (entry.placeholder.pixelsPerUnit == s) std::swap(entry.tiles, entry.placeholder);
            else
            {
                if (!entry.tiles.done.empty()) entry.placeholder = std::move(entry.tiles);
                entry.tiles = Tiles{ s };
            }
        }

        // The tiles of the drawn area; those it left give their bitmaps back.
        pdf::TileGrid grid = GridOf(size, s);
        std::vector<int> needed = grid.Covering(PixelsOf(area, s));
        std::set<int> neededSet(needed.begin(), needed.end());
        if (entry.pages.size() == 1) // a PDF page shown on several pages keeps the tiles of all their areas
        {
            std::erase_if(entry.tiles.done, [&](auto const& tile) { return !neededSet.contains(tile.first); });
            std::set<int> left;
            for (auto const& [index, request] : entry.tiles.asked)
                if (!neededSet.contains(index)) left.insert(index);
            DropTiles(entry.tiles, left);
        }
        bool complete = std::ranges::all_of(needed, [&](int index) { return entry.tiles.done.contains(index); });
        if (complete)
        {
            DrawTiles(dc, entry.tiles, size, false, area, dark);
            complete = std::ranges::all_of(needed, [&](int index) { return entry.tiles.done.contains(index); });
        }
        if (!complete)
        {
            DrawPart(dc, entry.preview, { 0, 0, size.width, size.height }, entry.previewWidth > size.width * s,
                dark ? &darkDrawing : nullptr);
            float oldScale = entry.placeholder.pixelsPerUnit;
            if (!entry.preview || oldScale * size.width >= entry.previewWidth)
                DrawTiles(dc, entry.placeholder, size, oldScale > s, area, dark);
            DrawTiles(dc, entry.tiles, size, false, area, dark);
            // For a small page the sharp render is already cheap. For larger pages, render one fixed preview only
            // when the old tiles do not cover the drawn area. A zoom change never resubmits that preview.
            float previewScale = static_cast<float>(PreviewSide) / std::max(size.width, size.height);
            if (asking && s > previewScale * 2 && !entry.preview && !entry.previewRequest)
            {
                bool covered = !entry.placeholder.done.empty() &&
                    std::ranges::all_of(GridOf(size, oldScale).Covering(PixelsOf(area, oldScale)),
                        [&](int index) { return entry.placeholder.done.contains(index); });
                if (!covered) RequestPreview(page, key, FitPixels(size.width, size.height, previewScale, PreviewSide));
            }
        }

        int missing = 0;
        for (int index : needed)
        {
            if (entry.tiles.done.contains(index)) continue;
            ++missing;
            if (asking && !entry.tiles.asked.contains(index))
            {
                RequestTile(page, key, s, index, grid.Rect(index), missing); // the area's middle first
            }
        }
        float oldScale = entry.placeholder.pixelsPerUnit;
        if (missing == 0 && (entry.placeholder.done.size() > 1 || size.width * size.height * oldScale * oldScale > 2'000'000))
            entry.placeholder = {};
    }

    void PdfBackground::DrawTiles(ID2D1DeviceContext* dc, Tiles& tiles, D2D1_SIZE_F size, bool shrunk, D2D1_RECT_F area, bool dark)
    {
        if (tiles.done.empty()) return;
        float k = tiles.pixelsPerUnit;
        pdf::TileGrid grid = GridOf(size, k);
        for (auto& [index, bitmap] : tiles.done)
        {
            pdf::PixelRect r = grid.Rect(index);
            D2D1_RECT_F dest{ r.left / k, r.top / k, r.right / k, r.bottom / k };
            if (dest.left < area.right && dest.right > area.left && dest.top < area.bottom && dest.bottom > area.top)
                DrawPart(dc, bitmap, dest, shrunk, dark ? &darkDrawing : nullptr);
        }
        std::erase_if(tiles.done, [](auto const& tile) { return !tile.second; }); // let go after a device change: asked again
    }

    void PdfBackground::DropTiles(Tiles& tiles, std::set<int> const& indices)
    {
        if (indices.empty()) return;
        std::set<std::uint64_t> keys;
        for (int index : indices)
        {
            auto request = tiles.asked.extract(index);
            if (!request.empty()) keys.insert(request.mapped());
        }
        queue.Drop([&](std::uint64_t key) { return keys.contains(key); }); // one already running is dropped when it arrives
    }

    void PdfBackground::DropTiles(Tiles& tiles)
    {
        std::set<int> indices;
        for (auto const& [index, request] : tiles.asked) indices.insert(index);
        DropTiles(tiles, indices);
    }

    void PdfBackground::Ready(PageKey const& key)
    {
        auto const& pages = rendered.at(key).pages;
        readyPages.insert(pages.begin(), pages.end());
        if (readyPosted) return;
        readyPosted = true;
        std::weak_ptr<PdfBackground> weak = weak_from_this();
        auto redraw = [weak]
        {
            auto self = weak.lock();
            if (!self) return;
            self->readyPosted = false;
            auto pages = std::move(self->readyPages);
            self->readyPages.clear();
            if (!pages.empty()) self->ready(pages);
        };
        if (!UiQueue::Post("pdf pages ready", redraw)) redraw();
    }

    void PdfBackground::RequestPreview(int page, PageKey key, PixelSize size)
    {
        int pdfPage = std::get<0>(key);
        auto request = NextRequest();
        rendered.at(key).previewRequest = request;
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(request, page, std::format("render PDF page {} preview at {} px", pdfPage + 1, size.width),
            [weak, source = source, key, request, pdfPage, size](PdfPageRenderer& renderer)
        {
            if (weak.expired()) return;
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
            UiQueue::Post("pdf page preview rendered", [weak, key, request, size, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                auto it = self->rendered.find(key);
                if (it == self->rendered.end() || it->second.previewRequest != request) return;
                Rendered& entry = it->second;
                entry.previewRequest = 0;
                entry.failed = entry.failed || failed;
                if (!bitmap) return;
                entry.preview = std::make_unique<DrawableBitmap>(bitmap, false);
                entry.previewWidth = size.width;
                self->Ready(key);
            });
        }, -1); // before the page's tiles: the placeholder comes first
    }

    void PdfBackground::RequestTile(int page, PageKey key, float pixelsPerUnit, int index, pdf::PixelRect pixels,
        int rank)
    {
        auto [pdfPage, pageWidth, pageHeight] = key;
        D2D1_SIZE_F size{ pageWidth, pageHeight };
        auto request = NextRequest();
        rendered.at(key).tiles.asked.emplace(index, request);
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        UINT width = static_cast<UINT>(pixels.Width()), height = static_cast<UINT>(pixels.Height());
        queue.Submit(request, page, std::format("render PDF page {} tile {}: {}x{} px at {:.3f} px/unit",
            pdfPage + 1, index, width, height, pixelsPerUnit), [weak, source = source, key, request, pdfPage, size, pixelsPerUnit, index, pixels, width,
            height](PdfPageRenderer& renderer)
        {
            if (weak.expired()) return;
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
                    // Use the same area filter as the thumbnails for small, reduced pages. Bound the
                    // extra GPU readback so supersampling cannot turn a large on-screen tile into a bottleneck.
                    constexpr double MaxFilteredPixels = 750'000;
                    double pagePixels = static_cast<double>(size.width) * size.height * pixelsPerUnit * pixelsPerUnit;
                    if (kx > 1 && ky > 1 && pagePixels <= MaxFilteredPixels)
                        bitmap = renderer.RenderThumbnail(pdfPageObject, width, height, false, part);
                    else
                        bitmap = renderer.Render(pdfPageObject, width, height, false, part);
                }
                catch (...)
                {
                    LogException(std::format("pdf background: page {}, tile {} (not asked again)", pdfPage + 1, index));
                    failed = true;
                }
            }
            else if (document)
            {
                LogError(std::format("pdf background: page {} asked, the PDF has {}", pdfPage + 1, document.PageCount()));
                failed = true;
            }
            UiQueue::Post("pdf page tile rendered", [weak, key, request, index, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                auto it = self->rendered.find(key);
                if (it == self->rendered.end()) return;
                Rendered& entry = it->second;
                auto asked = entry.tiles.asked.find(index);
                if (asked == entry.tiles.asked.end() || asked->second != request) return;
                entry.tiles.asked.erase(asked);
                entry.failed = entry.failed || failed;
                // Another zoom meanwhile, or the tile left the drawn area: not needed. Not loaded yet: asked again
                // when the PDF is there.
                if (!bitmap) return;
                entry.tiles.done[index] = std::make_unique<DrawableBitmap>(bitmap, false);
                self->Ready(key);
            });
        }, rank);
    }

    void PdfBackground::RenderPage(std::uint64_t key, int pdfPage, UINT width, UINT height, int priorityPage, bool dark,
        std::function<void(std::shared_ptr<CpuBitmap>)> done)
    {
        // A page move changes the priority index while the request is waiting.
        queue.Submit(key, priorityPage, std::format("render PDF page {} for a thumbnail", pdfPage + 1),
            [source = source, pdfPage, width, height, dark, done](PdfPageRenderer& renderer)
        {
            std::shared_ptr<CpuBitmap> bitmap;
            auto document = source->Document();
            if (document && static_cast<UINT>(pdfPage) < document.PageCount())
            {
                try
                {
                    bitmap = renderer.RenderThumbnail(document.GetPage(static_cast<UINT>(pdfPage)), width, height, false);
                    if (dark) DarkPdf::Recolor(*bitmap);
                }
                catch (...)
                {
                    LogException(std::format("pdf background: thumbnail of page {}", pdfPage + 1));
                }
            }
            UiQueue::Post("pdf thumbnail rendered", [done, bitmap] { done(bitmap); });
        }, RenderQueue::ThumbnailRank);
    }

    void PdfBackground::Remap(inkpages::PageMap const& map)
    {
        readyPages = map.Apply(readyPages);
        for (auto& [pdfPage, entry] : rendered)
        {
            entry.pages = map.Apply(entry.pages);
        }
    }

    void PdfBackground::RenderArea(int pdfPage, int documentPage, Rect area, double scale,
        std::function<void(std::shared_ptr<CpuBitmap>, std::exception_ptr)> done)
    {
        auto document = source->Document();
        // An explicit copy may finish after the user opens another document.
        queue.Submit(NextRequest() | (std::uint64_t{1} << 63), documentPage, "render PDF area for clipboard",
            [document, pdfPage, area, scale, done = std::move(done)](PdfPageRenderer& renderer) {
                std::shared_ptr<CpuBitmap> bitmap;
                try
                {
                    if (!document || pdfPage < 0 || static_cast<UINT>(pdfPage) >= document.PageCount())
                        ThrowInvalidData("PDF area: missing PDF page");
                    constexpr double dip = 96.0 / 72.0;
                    D2D1_RECT_F sourceRect{ static_cast<float>(area.x * dip), static_cast<float>(area.y * dip),
                        static_cast<float>((area.x + area.width) * dip), static_cast<float>((area.y + area.height) * dip) };
                    auto size = FitPixels(area.width, area.height, scale, 4096);
                    bitmap = renderer.Render(document.GetPage(static_cast<UINT>(pdfPage)), size.width, size.height,
                        true, sourceRect);
                }
                catch (...)
                {
                    done({}, std::current_exception());
                    return;
                }
                done(std::move(bitmap), {});
            }, -2);
    }

    void PdfBackground::Trim(int focus, int keep, std::vector<int> const& drawing)
    {
        queue.SetFocus(focus);
        for (auto& [pdfPage, entry] : rendered)
        {
            bool drawn = std::any_of(drawing.begin(), drawing.end(), [&](int page) { return entry.pages.contains(page); });
            if (drawn) continue;
            auto within = [&](int distance)
            {
                return std::any_of(entry.pages.begin(), entry.pages.end(), [&](int page) { return std::abs(page - focus) <= distance; });
            };
            // A render that has not started when its page left the view is dropped.
            if (!within(keep) && (entry.preview || entry.previewRequest))
            {
                entry.preview.reset();
                entry.previewWidth = 0;
                auto request = std::exchange(entry.previewRequest, 0);
                if (request) queue.Drop([request](std::uint64_t k) { return k == request; });
            }
            if (!within(SharpAround) && (!entry.tiles.done.empty() || !entry.tiles.asked.empty() || !entry.placeholder.done.empty()))
            {
                DropTiles(entry.tiles);
                entry.tiles = {};
                entry.placeholder = {};
            }
        }
    }
}
