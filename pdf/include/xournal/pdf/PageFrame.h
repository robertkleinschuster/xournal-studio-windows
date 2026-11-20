#pragma once

// How a PDF page is shown - as Windows.Data.Pdf renders it and the app draws it under a page: its crop box (else its
// media box), turned by /Rotate. "Shown" coordinates are points with the origin at the shown page's top left, y down
// (the app stretches the shown page over its page, so they scale by page size / Shown()).

#include "xournal/model/Geometry.h"

#include <optional>

namespace xournal::pdf
{
    /// A PDF rectangle in user space (any two opposite corners).
    struct PdfBox
    {
        double llx = 0, lly = 0, urx = 0, ury = 0;
    };

    struct PageFrame
    {
        double x1 = 0, y1 = 0, width = 0, height = 0; // the visible box in PDF user space (normalized)
        int rotate = 0;                                // 0, 90, 180 or 270

        /// The frame of a page: its crop box when it has an area, else its media box; /Rotate taken modulo 360.
        static PageFrame Of(std::optional<PdfBox> const& cropBox, PdfBox const& mediaBox, long long rotate);

        /// The shown page's size (width and height swap for 90 and 270).
        Size Shown() const;
        /// A point of the unrotated box measured from its top left (y down) on the shown page.
        Point FromTopLeft(double u, double v) const;
        /// A point in PDF user space on the shown page.
        Point FromUserSpace(double x, double y) const { return FromTopLeft(x - x1, y1 + height - y); }
        /// A point of the shown page in PDF user space (the inverse of FromUserSpace: hit-testing the PDF's content).
        Point ToUserSpace(Point shown) const;
        /// A rectangle in PDF user space on the shown page.
        Rect Area(PdfBox const& r) const;
    };
}
