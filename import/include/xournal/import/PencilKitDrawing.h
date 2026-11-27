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
//
// Writing (the iPad export) uses the older variant only: gzip + that protobuf message, laid out as iPadOS wrote it in the
// user's PDFs (2026-11-27, surveyed over their 10 older drawings: version fields 10, a drawing id, two ordering
// records {n,0,n} and {0,1,0}, per stroke {i,0,i+1}, {0,0,0}, ink index, point count, the 24-byte points with force 1000
// and aspect 1000, the bounds, the time, two ids) - iPadOS still reads it.

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <cmath>
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
        std::vector<double> widths; // each point's brush size, scaled by the stroke's transform (empty: `width` everywhere)

        bool IsMarker() const;
    };

    /// The pen's drawn width (pt) and PencilKit's brush size, one pair for the export and the import so that a round trip
    /// returns the same width (up to the tenths the older drawing format stores). Through the point the user's own pen
    /// strokes measured (2.5 <-> 1.0 pt, "2.5 -> 1.0 pt in eight drawings", 2026-11-18) the size follows the square root of
    /// the width: linear (width / 0.4) spread the widths on the iPad, thin thinner and thick thicker (user, 2026-11-27). An
    /// estimate away from that point (the user's strokes all lie at sizes 2.5 to 3.0): calibrate with an iPad-saved test.
    constexpr double PenSizeAtOnePoint = 2.5;
    inline double PenBrushSize(double drawnWidth) { return PenSizeAtOnePoint * std::sqrt(drawnWidth > 0 ? drawnWidth : 0); }
    inline double PenDrawnWidth(double brushSize)
    {
        double r = brushSize / PenSizeAtOnePoint;
        return r * r;
    }
    /// The marker is drawn about 1.2 of its size (measured as the pen's, only for the one size 5).
    constexpr double MarkerWidthFactor = 1.2;

    namespace PencilKitDrawing
    {
        /// The visible strokes in drawing order. Throws xournal::Error (invalid data) for data it cannot read.
        std::vector<PencilKitStroke> Read(std::span<std::uint8_t const> data);

        /// A drawing in the older variant (gzip) of `strokes` in drawing order (points in drawing coordinates, points, y
        /// down; `width` or `widths` = the brush size; the ink name and color from the stroke). Throws for no strokes.
        std::vector<std::uint8_t> WriteLegacy(std::span<PencilKitStroke const> strokes);

        /// The bounds WriteLegacy stores for a stroke: its points and half the widest brush size around them, rounded
        /// outwards to whole numbers as iPadOS stores them (drawing coordinates).
        Rect BoundsOf(PencilKitStroke const& stroke);
    }
}
