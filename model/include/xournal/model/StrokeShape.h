#pragma once

// A stroke as Xournal++ draws it - the one description the screen and the export both draw (strokes shown from the
// file: filled, dashed, butt or square ends, damaged).

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <optional>
#include <variant>
#include <vector>

namespace xournal
{
    enum class StrokeCap { Round, Butt, Square };

    /// The filled outline.
    struct StrokeFill
    {
        std::vector<Point> points;
    };

    /// A one-point stroke: a circle, or a square for square ends, of the stroke's width.
    struct StrokeDot
    {
        Point center;
        double size = 0;
        bool square = false;
    };

    /// A polyline of one width; `dashOffset` = the length drawn before it (points).
    struct StrokeLine
    {
        std::vector<Point> points;
        double width = 0;
        double dashOffset = 0;
    };

    using StrokePart = std::variant<StrokeFill, StrokeDot, StrokeLine>;

    struct StrokeShape
    {
        Color color;
        bool highlighter = false;
        double width = 0;
        std::vector<Point> points;
        std::vector<double> segmentWidths;
        int fill = -1;               // alpha (0-255) of the filled outline, -1 for none
        std::vector<double> dashes;  // page points, empty for solid
        StrokeCap cap = StrokeCap::Round;

        /// All segments have the stroke's width (no pressure): drawn as one polyline.
        bool EvenWidth() const;

        /// What is drawn, bottom to top: the filling (three points or more), then a dot (one point: its end shape,
        /// nothing for butt ends) or the line (one polyline; with pressure one per segment of positive width, the dashes
        /// running on).
        std::vector<StrokePart> Parts() const;

        /// The lowest point of what is drawn (an upper bound for square-ended dashes), or nothing when nothing is.
        std::optional<double> Bottom() const;

        /// The dash pattern for a line of `lineWidth` in multiples of it (as Direct2D and PDF pens take it), or nothing
        /// for a solid line - one rule for the screen and the export.
        std::optional<std::vector<double>> DashesFor(double lineWidth) const;
    };
}
