#pragma once

// Bitmaps of a document's pictures for the underlay: <image> data decoded by WIC (on first draw, kept), <teximage>
// data that is a one-page PDF rendered on the render thread at the zoom it is drawn, picture backgrounds from their
// files. A picture that cannot be read is logged once and draws nothing.

#include "Bitmaps.h"
#include "RenderQueue.h"

#include "xournal/model/Xml.h"

#include <d2d1_1.h>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace xournal::app
{
    class PictureBitmaps : public std::enable_shared_from_this<PictureBitmaps>
    {
    public:
        /// `ready(page)`: a rendered picture arrived (UI thread).
        PictureBitmaps(RenderQueue& queue, std::uint64_t generation, std::function<void(int page)> ready);

        /// Draws a picture element over `size` (its placement's size, page units) for document page `page`.
        void DrawElement(ID2D1DeviceContext* dc, XmlElementPtr const& element, int page, D2D1_SIZE_F size, float pixelsPerUnit);

        /// Draws a picture file (a picture background) over `size`.
        void DrawFile(ID2D1DeviceContext* dc, std::filesystem::path const& file, D2D1_SIZE_F size);

    private:
        struct Entry
        {
            XmlElementPtr element;                    // kept so the key (its address) stays its own
            std::unique_ptr<DrawableBitmap> bitmap;
            bool failed = false;
            bool pdf = false;
            UINT width = 0, requested = 0;            // PDF pictures: rendered / on its way
        };

        void RequestPdf(Entry& entry, int page, UINT width, UINT height);

        RenderQueue& queue;
        std::uint64_t generation;
        std::function<void(int)> ready;
        std::map<XmlElement const*, Entry> elements;
        std::map<std::wstring, Entry> files;
        std::uint32_t nextKey = 1;
        std::map<XmlElement const*, std::uint32_t> keys;
    };
}
