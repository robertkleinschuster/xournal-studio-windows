#include "PdfBackground.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr UINT MaxWidth = 4096; // sharp at 6x zoom on a 150 % screen for A4 widths, within Direct2D's limits

        // Render keys: the generation (document) in the high bits, the PDF page below.
        std::uint64_t KeyOf(std::uint64_t generation, int pdfPage) { return (generation << 32) | static_cast<std::uint32_t>(pdfPage); }
    }

    PdfBackground::PdfBackground(RenderQueue& queue, std::uint64_t generation, std::function<void(int)> ready)
        : queue(queue), generation(generation), ready(std::move(ready))
    {
    }

    void PdfBackground::Load(std::filesystem::path const& pdf)
    {
        std::weak_ptr<PdfBackground> weak = weak_from_this();
        queue.Submit(KeyOf(generation, -1), -1000000, "load background PDF", [weak, pdf](PdfPageRenderer&)
        {
            auto self = weak.lock();
            if (!self) return;
            try
            {
                auto data = io::Files::ReadAll(pdf);
                auto document = Bitmaps::LoadPdf(data);
                std::scoped_lock guard(self->documentLock);
                self->document = document;
                Log(std::format("pdf background: {} loaded, {} pages", ToUtf8(pdf.native()), document.PageCount()));
            }
            catch (...)
            {
                self->failed = true;
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

    void PdfBackground::Draw(ID2D1DeviceContext* dc, int page, int pdfPage, D2D1_SIZE_F size, float pixelsPerUnit)
    {
        Rendered& entry = rendered[pdfPage];
        entry.pages.insert(page);
        if (entry.bitmap)
        {
            dc->DrawBitmap(entry.bitmap->On(dc), { 0, 0, size.width, size.height }, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        }
        UINT width = std::min<UINT>(MaxWidth, static_cast<UINT>(std::ceil(size.width * pixelsPerUnit)));
        UINT height = static_cast<UINT>(std::ceil(width * size.height / size.width));
        bool sharpEnough = entry.width >= width * 0.95 && entry.width <= width * 1.5;
        if (!sharpEnough && entry.requested != width && width > 0 && !failed)
        {
            Request(page, pdfPage, width, height);
        }
    }

    void PdfBackground::Request(int page, int pdfPage, UINT width, UINT height)
    {
        rendered[pdfPage].requested = width;
        std::weak_ptr<PdfBackground> weak = weak_from_this();
        queue.Submit(KeyOf(generation, pdfPage), page, std::format("render PDF page {} at {} px", pdfPage + 1, width),
            [weak, pdfPage, width, height](PdfPageRenderer& renderer)
        {
            auto self = weak.lock();
            if (!self) return;
            winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
            {
                std::scoped_lock guard(self->documentLock);
                document = self->document;
            }
            if (!document)
            {
                // Not loaded yet (or failed): asked again when it is drawn after loading.
                UiQueue::Post("pdf page not ready", [weak, pdfPage]
                {
                    if (auto s = weak.lock()) s->rendered[pdfPage].requested = 0;
                });
                return;
            }
            if (static_cast<UINT>(pdfPage) >= document.PageCount())
            {
                LogError(std::format("pdf background: page {} asked, the PDF has {}", pdfPage + 1, document.PageCount()));
                return;
            }
            auto bitmap = renderer.Render(document.GetPage(static_cast<UINT>(pdfPage)), width, height, false);
            UiQueue::Post("pdf page rendered", [weak, pdfPage, width, bitmap]
            {
                auto s = weak.lock();
                if (!s) return;
                Rendered& entry = s->rendered[pdfPage];
                entry.bitmap = std::make_unique<DrawableBitmap>(bitmap);
                entry.width = width;
                if (entry.requested == width) entry.requested = 0;
                for (int page : entry.pages) s->ready(page);
            });
        });
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
