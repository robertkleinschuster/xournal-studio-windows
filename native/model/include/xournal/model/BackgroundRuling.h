#pragma once

// The rulings of solid page backgrounds, ported from upstream Xournal++'s background views (src/core/view/background:
// Ruled, Lined, Staves, Graph, Dotted, IsoDotted, IsoGraph) with their defaults and the background's "config" attribute
// (r1 raster, m1 margin, rm round margin, lw line width, bli/blw bold lines, f1/f2 colors, af1/af2 colors on dark
// paper). One description for the screen and the export.

#include "xournal/model/Background.h"
#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <string>
#include <utility>
#include <vector>

namespace xournal
{
    /// Lines (and dots, drawn as filled circles of `width`) of one color and width, in page points.
    struct RulingLines
    {
        Color color;
        double width = 0;
        std::vector<std::pair<Point, Point>> lines;
        std::vector<Point> dots;
    };

    namespace BackgroundRuling
    {
        /// The rulings of a background, bottom to top; empty for plain paper.
        std::vector<RulingLines> For(BackgroundStyle style, std::wstring const* config, Color paper, double width, double height);
    }
}
