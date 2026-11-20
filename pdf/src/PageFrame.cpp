#include "xournal/pdf/PageFrame.h"

#include <algorithm>
#include <cmath>

namespace xournal::pdf
{
    PageFrame PageFrame::Of(std::optional<PdfBox> const& cropBox, PdfBox const& mediaBox, long long rotate)
    {
        PdfBox box = cropBox && cropBox->urx != cropBox->llx && cropBox->ury != cropBox->lly ? *cropBox : mediaBox;
        auto turn = static_cast<int>(((rotate % 360) + 360) % 360);
        return { std::min(box.llx, box.urx), std::min(box.lly, box.ury), std::abs(box.urx - box.llx),
                 std::abs(box.ury - box.lly), turn - turn % 90 };
    }

    Size PageFrame::Shown() const
    {
        return rotate % 180 == 0 ? Size{ width, height } : Size{ height, width };
    }

    Point PageFrame::FromTopLeft(double u, double v) const
    {
        switch (rotate)
        {
        case 90: return { height - v, u };
        case 180: return { width - u, height - v };
        case 270: return { v, width - u };
        default: return { u, v };
        }
    }

    Point PageFrame::ToUserSpace(Point shown) const
    {
        double u = shown.x, v = shown.y; // the unrotated box's point from its top left
        switch (rotate)
        {
        case 90: u = shown.y; v = height - shown.x; break;
        case 180: u = width - shown.x; v = height - shown.y; break;
        case 270: u = width - shown.y; v = shown.x; break;
        default: break;
        }
        return { x1 + u, y1 + height - v };
    }

    Rect PageFrame::Area(PdfBox const& r) const
    {
        auto a = FromUserSpace(std::min(r.llx, r.urx), std::min(r.lly, r.ury));
        auto b = FromUserSpace(std::max(r.llx, r.urx), std::max(r.lly, r.ury));
        return Rect::FromCorners(a, b);
    }
}
