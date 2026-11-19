#pragma once

// Internal: builds a PDF content stream (ISO 32000-1, 8 and 9) as text, numbers written locale-independently.
// Coordinates get 3 decimals (1/1000 pt), transforms 5 (scale factors near 1 must not round to it).

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"
#include "xournal/model/StrokeShape.h"

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace xournal::pdf::detail
{
    /// Fixed point with at most `decimals` decimals, no exponent, no trailing zeros ("-0" -> "0").
    std::string Number(double value, int decimals = 3);

    class ContentStream
    {
    public:
        std::string const& Text() const { return text; }

        ContentStream& Raw(std::string_view s);
        ContentStream& Save() { return Raw("q\n"); }
        ContentStream& Restore() { return Raw("Q\n"); }
        ContentStream& Transform(double a, double b, double c, double d, double e, double f);
        ContentStream& Transform(Matrix const& m) { return Transform(m.m11, m.m12, m.m21, m.m22, m.m31, m.m32); }

        ContentStream& MoveTo(Point p);
        ContentStream& LineTo(Point p);
        ContentStream& CurveTo(Point c1, Point c2, Point p);
        ContentStream& ClosePath() { return Raw("h\n"); }
        ContentStream& Rectangle(double x, double y, double width, double height);
        /// A circle as four Bezier curves (closed subpath).
        ContentStream& Circle(Point center, double radius);
        ContentStream& Polyline(std::span<Point const> points);

        ContentStream& StrokePath() { return Raw("S\n"); }
        ContentStream& FillNonZero() { return Raw("f\n"); }
        ContentStream& FillEvenOdd() { return Raw("f*\n"); }

        ContentStream& StrokeColor(Color c); // RGB; alpha goes through an ExtGState
        ContentStream& FillColor(Color c);
        ContentStream& LineWidth(double width);
        ContentStream& Cap(StrokeCap cap);
        ContentStream& RoundJoin() { return Raw("1 j\n"); }
        ContentStream& Dash(std::span<double const> lengths, double phase);
        ContentStream& GraphicsState(std::string_view name);
        ContentStream& Draw(std::string_view xobjectName);

    private:
        ContentStream& Numbers(std::initializer_list<double> values, int decimals, std::string_view op);
        std::string text;
    };
}
