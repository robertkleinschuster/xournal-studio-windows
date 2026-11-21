#pragma once

// Page thumbnails drawn with GDI (the pages pane, the insert dialog): a list item's image slot holds a transparent
// image (it only gives the item its place); the page - white paper of its proportion, fitted into the slot's box and
// centered - is drawn over it in custom draw with its layers (the PDF page, what lies on it; premultiplied BGRA) and a
// thin border around. The layers are rendered at exactly ThumbnailSize, so they are drawn 1:1 (GDI's AlphaBlend
// samples the nearest pixel when it stretches: fine lines alias).

#include "PdfPageRenderer.h"

#include <windows.h>
#include <commctrl.h>

namespace xournal::app
{
    // GDI source for a thumbnail layer. Upload once when the render arrives.
    class ThumbnailBitmap
    {
    public:
        explicit ThumbnailBitmap(CpuBitmap const& pixels);
        ~ThumbnailBitmap();
        ThumbnailBitmap(ThumbnailBitmap const&) = delete;
        ThumbnailBitmap& operator=(ThumbnailBitmap const&) = delete;
        void Draw(HDC target, RECT const& area) const;

    private:
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        SIZE size{};
    };

    /// A page of `aspect` (height / width) fitted into `box` pixels, at least 1 x 1.
    SIZE ThumbnailSize(SIZE box, double aspect);

    /// An image list holding one fully transparent image of `slot` pixels.
    HIMAGELIST CreateSlotImages(SIZE slot);

    /// The page fitted into `box` and centered in `area` (the item's icon rectangle), its layers over the paper color.
    void DrawCachedPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect,
        ThumbnailBitmap const* background, ThumbnailBitmap const* content, bool darkPaper = false);
    /// The insert dialog may hold thumbnails for every page. Keep its GDI resources only while drawing.
    void DrawPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect, CpuBitmap const* background);
}
