#pragma once

// Xournal's <stroke> elements: reading their points and look, turning them into ink and ink back into elements, moving
// them. Page coordinates are Xournal points, used 1:1 as the ink's units.

#include "xournal/model/Ink.h"
#include "xournal/model/StrokeShape.h"
#include "xournal/model/Xml.h"

#include <optional>
#include <string>
#include <vector>

namespace xournal::Strokes
{
    /// The stroke as Xournal draws it (color with the file's alpha, points, per-segment widths, fill, dashes, caps);
    /// nothing without points.
    std::optional<StrokeShape> ReadShape(XmlElement const& e);

    /// The dash pattern of the "style" attribute in page points (upstream StrokeStyle::parseStyle: "dash", "dashdot",
    /// "dot" or "cust: a b ..."); empty for a solid line or a pattern nothing can draw.
    std::vector<double> Dashes(std::wstring const* style);

    /// A stroke shown (and exported) from the file, never edited as ink, so the file keeps it as it is: one Windows ink
    /// cannot draw as the file means it (filled, with a line style, with other than round ends) or a damaged one.
    bool NeedsOwnDrawing(XmlElement const& e);

    /// The element as ink, or nothing when it cannot be: no points, or NeedsOwnDrawing.
    std::optional<StrokeInk> InkOf(XmlElement const& e);

    /// The element as ink without the NeedsOwnDrawing check (reads as far as it can); nothing without points.
    std::optional<StrokeInk> FromXml(XmlElement const& e);

    /// The ink as a <stroke> element, its samples placed by `transform`: tool, color (a highlighter with Xournal's
    /// alpha 0x7f), width at full pressure plus per-segment widths for a pressure stroke.
    XmlElementPtr ToXml(StrokeInk const& ink, Matrix const& transform = {});

    /// Moves/scales the points of a stroke element as the ink's PointTransform does (widths stay); what follows the
    /// points (a damaged number and everything after it) and all attributes are kept as they are.
    void Transform(XmlElement& e, Matrix const& m);
}
