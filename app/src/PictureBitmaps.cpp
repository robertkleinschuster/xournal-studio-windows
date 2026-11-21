#include "PictureBitmaps.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppImage.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr UINT MaxWidth = 4096;

        void DrawOver(ID2D1DeviceContext* dc, DrawableBitmap& bitmap, D2D1_SIZE_F size)
        {
            dc->DrawBitmap(bitmap.On(dc), { 0, 0, size.width, size.height }, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        }
    }

    PictureBitmaps::PictureBitmaps(RenderQueue& queue, std::uint64_t generation, std::function<void(int)> ready)
        : queue(queue), generation(generation), ready(std::move(ready))
    {
    }

    void PictureBitmaps::DrawElement(ID2D1DeviceContext* dc, XmlElementPtr const& element, int page, D2D1_SIZE_F size,
        float pixelsPerUnit, bool dark)
    {
        auto [it, added] = elements.try_emplace(element.get());
        Entry& entry = it->second;
        if (added)
        {
            entry.element = element;
            auto data = XoppImage::GetData(*element);
            if (XoppImage::IsPdf(data))
            {
                entry.pdf = std::make_shared<PdfSource>();
                entry.pdf->data = std::move(data);
            }
            else
            {
                try
                {
                    entry.bitmap = std::make_unique<DrawableBitmap>(Bitmaps::Decode(data));
                }
                catch (...)
                {
                    entry.failed = true;
                    LogException(std::format("picture <{}> on page {} cannot be read", ToUtf8(element->Name()), page + 1));
                }
            }
        }
        if (entry.failed)
        {
            return;
        }
        if (entry.bitmap)
        {
            if (dark && entry.pdf)
                darkDrawing.Draw(dc, entry.bitmap->On(dc), { 0, 0, size.width, size.height }, D2D1_INTERPOLATION_MODE_LINEAR);
            else DrawOver(dc, *entry.bitmap, size);
        }
        if (entry.pdf)
        {
            PixelSize wanted = FitPixels(size.width, size.height, pixelsPerUnit, MaxWidth);
            if ((entry.width < wanted.width * 0.95 || entry.width > wanted.width * 1.5) && entry.requested != wanted.width)
            {
                RequestPdf(entry, page, wanted);
            }
        }
    }

    void PictureBitmaps::RequestPdf(Entry& entry, int page, PixelSize size)
    {
        entry.requested = size.width;
        XmlElement const* id = entry.element.get();
        auto [keyIt, added] = keys.try_emplace(id, nextKey);
        if (added) ++nextKey;
        // Picture keys live beside the background's PDF pages: bit 31 set.
        std::uint64_t key = (generation << 32) | 0x80000000u | keyIt->second;
        std::weak_ptr<PictureBitmaps> weak = weak_from_this(); // locked only on the UI thread
        queue.Submit(key, page, std::format("render PDF picture on page {}", page + 1),
            [weak, source = entry.pdf, id, page, size](PdfPageRenderer& renderer)
        {
            std::shared_ptr<CpuBitmap> bitmap;
            try
            {
                if (!source->document && !source->failed)
                {
                    source->document = Bitmaps::LoadPdf(source->data); // once per picture, not per zoom step
                }
                if (source->document)
                {
                    bitmap = renderer.Render(source->document.GetPage(0), size.width, size.height, true);
                }
            }
            catch (...)
            {
                source->failed = true;
                LogException(std::format("PDF picture on page {} cannot be shown (not tried again)", page + 1));
            }
            bool failed = source->failed;
            UiQueue::Post("pdf picture rendered", [weak, id, page, size, bitmap, failed]
            {
                auto self = weak.lock();
                if (!self) return;
                auto it = self->elements.find(id);
                if (it == self->elements.end()) return;
                if (it->second.requested == size.width) it->second.requested = 0;
                it->second.failed = failed;
                if (!bitmap) return;
                it->second.bitmap = std::make_unique<DrawableBitmap>(bitmap);
                it->second.width = size.width;
                self->ready(page);
            });
        });
    }

    void PictureBitmaps::DrawFile(ID2D1DeviceContext* dc, std::filesystem::path const& file, D2D1_SIZE_F size)
    {
        auto [it, added] = files.try_emplace(file.native());
        Entry& entry = it->second;
        if (added)
        {
            try
            {
                entry.bitmap = std::make_unique<DrawableBitmap>(Bitmaps::DecodeFile(file));
            }
            catch (...)
            {
                entry.failed = true; // a missing picture background shows white, as upstream
                LogException(std::format("picture background {} cannot be read", ToUtf8(file.native())));
            }
        }
        if (entry.bitmap)
        {
            DrawOver(dc, *entry.bitmap, size);
        }
    }
}
