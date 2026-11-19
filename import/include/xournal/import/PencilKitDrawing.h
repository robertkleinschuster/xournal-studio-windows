#pragma once

// Apple's PencilKit drawings (PKDrawing.dataRepresentation), the ink of iPad markup in PDFs. Apple does not document the
// format: the layout follows two decoders validated against Apple's framework - libfreeform's src/pkdrawing.rs
// (Apache-2.0) and notability-goodnotes' gnnote/pencilkit.py (MIT) - checked against the appearance images of the user's
// iPad PDFs (2026-11-18). Two variants occur:
// - "wrd\xf0" + version (u16), then a protobuf message: inks (#4: color #1 {r,g,b,a}, identifier #2) and strokes (#5:
//   ink #4, path #5, transform #7, visible pieces #11 of an erased stroke: ink #4, transform #7, mask polygons #10,
//   parametric ranges #12); a path has the point count (#3), the masks of per-point (#4) and constant (#5) channels, the
//   constant (#6) and per-point (#7) records.
// - older drawings: gzip, then a protobuf message whose #2.#3 holds inks (#4) and strokes (#5: ink #3, point count #4,
//   24-byte points #5: time, x, y as f32, then width x 10, aspect, edge, force, azimuth, altitude as u16).
// The stored points are the curve's control points; they become the stroke's polyline (a point or two apart). Pressure
// is not taken over: the user's drawings have force 1 everywhere. Port of the C# PencilKitDrawing.

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xournal::import
{
    /// A visible stroke: drawing coordinates (points, y down), PencilKit's own width.
    struct PencilKitStroke
    {
        std::string ink;            // e.g. "com.apple.ink.pen", "com.apple.ink.marker"
        Color color;                // the ink's color, its alpha lowered by the points' opacity (pencil)
        std::vector<Point> points;
        double width = 0;           // the points' median width (brush size), scaled by the stroke's transform

        bool IsMarker() const;
    };

    namespace PencilKitDrawing
    {
        /// The visible strokes in drawing order. Throws xournal::Error (invalid data) for data it cannot read.
        std::vector<PencilKitStroke> Read(std::span<std::uint8_t const> data);
    }
}
