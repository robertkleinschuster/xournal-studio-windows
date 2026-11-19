#pragma once

// The model's plain ink (xournal::StrokeInk) and Windows ink strokes (what InkPages holds): polylines without curve
// fitting, round pen tip, pressure unless the stroke ignores it.

#include "xournal/model/Ink.h"

#include "inkpages/WritingSurface.h"

namespace xournal::app::InkConvert
{
    /// A Windows ink stroke of these samples and look (any thread).
    inkpages::InkStroke ToInkStroke(StrokeInk const& ink);

    /// The stroke's samples (its PointTransform applied) and look.
    StrokeInk FromInkStroke(inkpages::InkStroke const& stroke);
}
