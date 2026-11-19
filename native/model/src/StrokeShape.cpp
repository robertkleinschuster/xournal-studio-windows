#include "xournal/model/StrokeShape.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace xournal
{
    bool StrokeShape::EvenWidth() const
    {
        return std::all_of(segmentWidths.begin(), segmentWidths.end(), [&](double w) { return w == width; });
    }

    std::vector<StrokePart> StrokeShape::Parts() const
    {
        std::vector<StrokePart> parts;
        if (fill >= 0 && points.size() > 2)
        {
            parts.push_back(StrokeFill{ points });
        }
        if (points.size() == 1)
        {
            if (width > 0 && cap != StrokeCap::Butt)
            {
                parts.push_back(StrokeDot{ points[0], width, cap == StrokeCap::Square });
            }
        }
        else if (EvenWidth())
        {
            if (width > 0)
            {
                parts.push_back(StrokeLine{ points, width, 0 });
            }
        }
        else
        {
            double drawn = 0;
            for (size_t i = 0; i + 1 < points.size(); ++i)
            {
                Point a = points[i], b = points[i + 1];
                if (segmentWidths[i] > 0)
                {
                    parts.push_back(StrokeLine{ { a, b }, segmentWidths[i], drawn });
                }
                drawn += std::hypot(b.x - a.x, b.y - a.y);
            }
        }
        return parts;
    }

    namespace
    {
        double LowestY(std::vector<Point> const& points)
        {
            double lowest = -std::numeric_limits<double>::infinity();
            for (auto const& p : points)
            {
                lowest = std::max(lowest, p.y);
            }
            return lowest;
        }
    }

    std::optional<double> StrokeShape::Bottom() const
    {
        // A line reaches half its width below its points (round ends and joins too); a square end reaches half the width
        // beyond the end, also sideways: at most sqrt(2) * width / 2 below it - at the line's two ends, or (an upper
        // bound) at any point of a dashed line, where a dash may end.
        auto squareEnds = [&](StrokeLine const& l)
        {
            if (cap != StrokeCap::Square)
            {
                return -std::numeric_limits<double>::infinity();
            }
            double lowest = DashesFor(l.width) ? LowestY(l.points) : std::max(l.points.front().y, l.points.back().y);
            return lowest + std::sqrt(2.0) * l.width / 2;
        };
        std::optional<double> bottom;
        for (auto const& part : Parts())
        {
            double partBottom = std::visit([&](auto const& p) -> double
            {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, StrokeFill>)
                    return LowestY(p.points);
                else if constexpr (std::is_same_v<T, StrokeDot>)
                    return p.center.y + p.size / 2;
                else
                    return std::max(LowestY(p.points) + p.width / 2, squareEnds(p));
            }, part);
            bottom = bottom ? std::max(*bottom, partBottom) : partBottom;
        }
        return bottom;
    }

    std::optional<std::vector<double>> StrokeShape::DashesFor(double lineWidth) const
    {
        if (dashes.empty() || lineWidth <= 0 || !std::isfinite(*std::max_element(dashes.begin(), dashes.end()) / lineWidth))
        {
            return std::nullopt;
        }
        std::vector<double> relative;
        for (double d : dashes)
        {
            relative.push_back(d / lineWidth);
        }
        return relative;
    }
}
