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
#include <vector>

namespace xournal::app
{
    class PageUnderlay
    {
    public:
        /// `pdfLoaded`: the background PDF (and its navigation) is loaded - also after LoadPdf (UI thread).
        PageUnderlay(RenderQueue& queue, XoppDocument const& doc, std::uint64_t generation, std::function<void(int)> invalidate,
            std::function<void(std::set<int> const&)> invalidateBatch,
            FilePictures const& pictures, std::function<void()> pdfLoaded);

        void Draw(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area, float pixelsPerUnit, bool darkPaper);

        /// One picture or text of `page` as Draw draws it (page units; the drag layer draws the selection with it).
        void DrawObject(int page, XmlElementPtr const& element, ID2D1DeviceContext* dc, float pixelsPerUnit, bool darkPaper,
            bool darkPictures);

        /// Pages were inserted, removed or moved: the caches follow.
        void Remap(inkpages::PageMap const& map);

        /// The document's PDF changed (pages appended to it, or it has a PDF now): loaded anew from memory.
        void LoadPdf(std::filesystem::path const& file);

        /// The background PDF on screen (thumbnails render its pages too).
        std::shared_ptr<PdfBackground> Pdf() const { return pdf; }

        /// The page in view changed: far pages give back their bitmaps, near ones render first.
        void Trim(int focus, std::vector<int> const& drawing);

        /// Marks a search hit on `page` (rectangles in page units) over everything else of the underlay; page -1: none.
        void SetHighlight(int page, std::vector<Rect> rects);
        int HighlightedPage() const { return highlight.page; }
        /// Marks the mouse's selection of the PDF's text the same way (its own mark beside the search hit's).
        void SetTextSelection(int page, std::vector<Rect> rects);
        int TextSelectionPage() const { return textSelection.page; }

    private:
        void DrawBackground(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area,
            float pixelsPerUnit, bool darkPaper);
        void DrawPicture(int page, XmlElementPtr const& picture, ID2D1DeviceContext* dc, float pixelsPerUnit, bool dark);
        struct Mark
        {
            int page = -1;
            std::vector<Rect> rects;
        };
        static void DrawMark(ID2D1DeviceContext* dc, Mark const& mark);

        RenderQueue& queue;
        std::uint64_t generation;
        std::function<void(std::set<int> const&)> invalidateBatch;
        std::function<void()> pdfLoaded;
        std::shared_ptr<PdfBackground> pdf;
        std::shared_ptr<PictureBitmaps> bitmaps;
        TextDrawing texts;
        FilePictures const& pictures;
        Mark highlight;     // the search hit
        Mark textSelection; // the PDF's text selected with the mouse
    };
}
