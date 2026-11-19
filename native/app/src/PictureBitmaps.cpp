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
        float pixelsPerUnit)
    {
        auto [it, added] = elements.try_emplace(element.get());
        Entry& entry = it->second;
        if (added)
        {
            entry.element = element;
            auto data = XoppImage::GetData(*element);
            entry.pdf = XoppImage::IsPdf(data);
            if (!entry.pdf)
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
            DrawOver(dc, *entry.bitmap, size);
        }
        if (entry.pdf)
        {
            UINT width = std::clamp<UINT>(static_cast<UINT>(std::ceil(size.width * pixelsPerUnit)), 1, MaxWidth);
            UINT height = std::max<UINT>(1, static_cast<UINT>(std::ceil(width * size.height / std::max(size.width, 0.01f))));
            if ((entry.width < width * 0.95 || entry.width > width * 1.5) && entry.requested != width)
            {
                RequestPdf(entry, page, width, height);
            }
        }
    }

    void PictureBitmaps::RequestPdf(Entry& entry, int page, UINT width, UINT height)
    {
        entry.requested = width;
        auto [keyIt, added] = keys.try_emplace(entry.element.get(), nextKey);
        if (added) ++nextKey;
        // Picture keys live beside the background's PDF pages: bit 31 set.
        std::uint64_t key = (generation << 32) | 0x80000000u | keyIt->second;
        std::weak_ptr<PictureBitmaps> weak = weak_from_this();
        XmlElementPtr element = entry.element;
        auto data = std::make_shared<std::vector<std::uint8_t>>(XoppImage::GetData(*element));
        queue.Submit(key, page, std::format("render PDF picture on page {}", page + 1),
            [weak, element, data, page, width, height](PdfPageRenderer& renderer)
        {
            auto document = Bitmaps::LoadPdf(*data);
            auto bitmap = renderer.Render(document.GetPage(0), width, height, true);
            UiQueue::Post("pdf picture rendered", [weak, element, page, width, bitmap]
            {
                auto self = weak.lock();
                if (!self) return;
                auto it = self->elements.find(element.get());
                if (it == self->elements.end()) return;
                it->second.bitmap = std::make_unique<DrawableBitmap>(bitmap);
                it->second.width = width;
                it->second.requested = 0;
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
