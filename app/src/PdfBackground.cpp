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
        constexpr UINT MaxWidth = 4096; // sharp at 6x zoom on a 150 % screen for A4 widths

        // Render keys: the generation (document) in the high bits, the PDF page below.
        std::uint64_t KeyOf(std::uint64_t generation, int pdfPage) { return (generation << 32) | static_cast<std::uint32_t>(pdfPage); }
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
                }
            });
        });
    }

    bool PdfBackground::Loaded() const
    {
        return source->Document() != nullptr;
    }

    std::shared_ptr<pdf::PdfNavigation const> PdfBackground::Navigation() const
    {
        std::scoped_lock guard(source->lock);
        return source->navigation;
    }

    void PdfBackground::Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, float pixelsPerUnit)
    {
        Rendered& entry = rendered[pdfPage];
        entry.pages.insert(page);
        if (entry.bitmap)
        {
            dc->DrawBitmap(entry.bitmap->On(dc), { 0, 0, size.width, size.height }, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        }
        PixelSize wanted = FitPixels(size.width, size.height, pixelsPerUnit, MaxWidth);
        bool sharpEnough = entry.width >= wanted.width * 0.95 && entry.width <= wanted.width * 1.5;
        if (!sharpEnough && entry.requested != wanted.width && wanted.width > 0 && !entry.failed && !source->failed)
        {
            Request(page, pdfPage, wanted);
        }
    }

    void PdfBackground::Request(int page, int pdfPage, PixelSize size)
    {
        rendered[pdfPage].requested = size.width;
        std::weak_ptr<PdfBackground> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(KeyOf(generation, pdfPage), page, std::format("render PDF page {} at {} px", pdfPage + 1, size.width),
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
            UiQueue::Post("pdf page rendered", [weak, pdfPage, size, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                Rendered& entry = self->rendered[pdfPage];
                if (entry.requested == size.width) entry.requested = 0;
                entry.failed = failed;
                if (!bitmap) return;
                entry.bitmap = std::make_unique<DrawableBitmap>(bitmap);
                entry.width = size.width;
                for (int page : entry.pages) self->ready(page);
            });
        });
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
            bool inView = std::any_of(entry.pages.begin(), entry.pages.end(), [&](int page) { return std::abs(page - focus) <= keep; });
            if (!inView && (entry.bitmap || entry.requested))
            {
                entry.bitmap.reset();
                entry.width = 0;
                entry.requested = 0;
                std::uint64_t key = KeyOf(generation, pdfPage);
                queue.Drop([key](std::uint64_t k) { return k == key; }); // left the view before its turn: not rendered
            }
        }
    }
}
