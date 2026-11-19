#include "xournal/pdf/PdfPlaces.h"

#include <algorithm>
#include <cstdlib>

namespace xournal::pdf
{
    namespace
    {
        // Page units per shown point, both ways (a page without a known PDF size shows it 1:1).
        Size ScaleOf(XoppPage const& page, ShownSizeOf const& shownSize)
        {
            auto shown = shownSize ? shownSize(page.PdfPageIndex()) : std::nullopt;
            if (!shown || shown->width <= 0 || shown->height <= 0) return { 1, 1 };
            return { page.Width() / shown->width, page.Height() / shown->height };
        }
    }

    std::optional<DocumentPlace> FindPlace(XoppDocument const& doc, Destination const& destination, int nearPage,
        ShownSizeOf const& shownSize)
    {
        auto const& pages = doc.Pages();
        int best = -1;
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        {
            if (pages[i]->IsPdf() && pages[i]->PdfPageIndex() == destination.page &&
                (best < 0 || std::abs(i - nearPage) < std::abs(best - nearPage)))
            {
                best = i;
            }
        }
        if (best < 0) return std::nullopt;
        DocumentPlace place{ best, {} };
        if (destination.position)
        {
            auto const& page = *pages[best];
            Size scale = ScaleOf(page, shownSize);
            place.point = { std::clamp(destination.position->x * scale.width, 0.0, page.Width()),
                std::clamp(destination.position->y * scale.height, 0.0, page.Height()) };
        }
        return place;
    }

    std::optional<Point> ToShown(XoppPage const& page, Point point, ShownSizeOf const& shownSize)
    {
        if (!page.IsPdf()) return std::nullopt;
        Size scale = ScaleOf(page, shownSize);
        return Point{ point.x / scale.width, point.y / scale.height };
    }

    Rect FromShown(XoppPage const& page, Rect area, ShownSizeOf const& shownSize)
    {
        Size scale = ScaleOf(page, shownSize);
        return { area.x * scale.width, area.y * scale.height, area.width * scale.width, area.height * scale.height };
    }
}
