#pragma once

// A page thumbnail drawn with GDI (the pages pane, the insert dialog): white paper of the page's proportion centered in
// an item's image area, its layers (the PDF page, what lies on it; premultiplied BGRA) alpha-blended over it, a thin
// border.

#include "PdfPageRenderer.h"

#include <windows.h>

namespace xournal::app
{
    void DrawPageThumbnail(HDC dc, RECT const& area, double aspect, CpuBitmap const* background, CpuBitmap const* content);
}
