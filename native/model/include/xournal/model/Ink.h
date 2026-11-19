#pragma once

// Strokes as the ink layer takes them: plain samples with pressure and a look - independent of the ink API that draws
// them (InkPages on Windows ink). While a page has a view, its strokes live in the ink layer; the page sees them through
// InkView and writes them back into its XML (see PageInk).

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <cstdint>
#include <vector>

namespace xournal
{
    /// The ink layer's identity of a stroke (Windows ink's InkStroke.Id).
    using StrokeId = std::uint32_t;

    struct InkSample
    {
        Point position;
        float pressure = 0.5f;
        bool operator==(InkSample const&) const = default;
    };

    struct InkLook
    {
        Color color = Colors::Black; // opaque; a highlighter's transparency is the ink's
        double size = 1;             // Windows ink's size: the drawn width at half pressure
        bool ignorePressure = true;
        bool highlighter = false;
        bool operator==(InkLook const&) const = default;
    };

    /// A stroke as ink: polyline samples (drawn without curve fitting, as Xournal++ draws) and its look.
    struct StrokeInk
    {
        std::vector<InkSample> samples;
        InkLook look;
        bool operator==(StrokeInk const&) const = default;
    };

    /// Windows ink draws a stroke wider with pressure (measured): exactly its size at p = 0.5, 1.75 times it at p = 1.
    /// Xournal++'s width is the width at full pressure, so a pen of Xournal width w is ink of size w / 1.75.
    namespace InkWidth
    {
        constexpr double Factor(double pressure) { return 0.25 + 1.5 * pressure; }
        constexpr double PressureFor(double factor) { return (factor - 0.25) / 1.5; }
        constexpr double FullPressureFactor = Factor(1);
    }

    /// A page's strokes in the ink layer while the page has a view (implemented by the app over InkPages).
    class InkView
    {
    public:
        virtual ~InkView() = default;
        /// The page's strokes in drawing order.
        virtual std::vector<StrokeId> Strokes() const = 0;
        /// The transform the ink shows the stroke with (a move or scale; Windows ink's PointTransform).
        virtual Matrix TransformOf(StrokeId id) const = 0;
        /// The stroke's untransformed samples and look.
        virtual StrokeInk InkOf(StrokeId id) const = 0;
    };
}
