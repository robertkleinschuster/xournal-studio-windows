#pragma once

// What a page shows below the ink, drawn with Direct2D into InkPages' page underlay (page coordinates = Xournal points),
// in upstream's order: the background (paper color and rulings, the background PDF's page, a picture), then each layer's
// elements in file order - strokes the ink cannot draw (StrokeShape), pictures (with a drag's preview transform), texts.
// One per shown document; UI thread (the caller holds the document's lock).

#include "FilePictures.h"
#include "PdfBackground.h"
#include "PictureBitmaps.h"
#include "RenderQueue.h"
#include "TextDrawing.h"

#include "xournal/model/XoppDocument.h"

#include <d2d1_1.h>

#include "inkpages/model/PageLayout.h"

#include <functional>
#include <memory>

namespace xournal::app
{
    class PageUnderlay
    {
    public:
        PageUnderlay(RenderQueue& queue, XoppDocument const& doc, std::uint64_t generation, std::function<void(int)> invalidate,
            FilePictures const& pictures);

        void Draw(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area, float pixelsPerUnit);

        /// The page in view changed: far pages give back their bitmaps, near ones render first.
        void Trim(int focus);

    private:
        void DrawBackground(int page, XoppPage const& content, ID2D1DeviceContext* dc, float pixelsPerUnit);
        void DrawPicture(int page, XmlElementPtr const& picture, ID2D1DeviceContext* dc, float pixelsPerUnit);

        std::shared_ptr<PdfBackground> pdf;
        std::shared_ptr<PictureBitmaps> bitmaps;
        TextDrawing texts;
        FilePictures const& pictures;
    };
}
