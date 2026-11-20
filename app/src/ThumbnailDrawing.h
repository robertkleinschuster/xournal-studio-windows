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
    /// A page of `aspect` (height / width) fitted into `box` pixels, at least 1 x 1.
    SIZE ThumbnailSize(SIZE box, double aspect);

    /// An image list holding one fully transparent image of `slot` pixels.
    HIMAGELIST CreateSlotImages(SIZE slot);

    /// The page fitted into `box` and centered in `area` (the item's icon rectangle), its layers over white.
    void DrawPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect, CpuBitmap const* background, CpuBitmap const* content);
}
