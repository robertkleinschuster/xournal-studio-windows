#pragma once

// What a page shows below the ink, drawn with Direct2D into InkPages' page underlay (page coordinates = Xournal
// points): the background (paper color and upstream's rulings, BackgroundRuling) and the strokes the ink cannot draw
// (filled, dashed, butt/square ends, damaged: StrokeShape). Still to come: PDF pages, picture backgrounds, pictures, texts.

#include "xournal/model/XoppPage.h"

#include <d2d1_1.h>

#include "inkpages/model/PageLayout.h"

namespace xournal::app::PageUnderlay
{
    /// Draws the page (UI thread; the caller holds the document's lock).
    void Draw(XoppPage const& page, ID2D1DeviceContext* dc, inkpages::RectF area);
}
