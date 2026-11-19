#pragma once

// A clip (model's XoppClip: copied strokes, pictures, texts relative to its top left) as a picture for other apps'
// clipboard formats: drawn by the PDF export onto one transparent page of the clip's size in memory and rendered by
// Windows.Data.Pdf (PdfAreaRenderer) - the same drawing as the export and the thumbnails. Blocking WinRT calls: any
// thread but an STA.

#include "xournal/model/Xml.h"
#include "xournal/pdf/PdfAreaRenderer.h"

namespace xournal::pdf
{
    /// The clip as a PNG (straight alpha, transparent where nothing is drawn) at `pixelsPerPoint`, at most `maxSide`
    /// pixels on its longer side. Throws xournal::Error.
    RenderedArea RenderClip(XmlElement const& clip, double pixelsPerPoint, int maxSide = 4096);
}
