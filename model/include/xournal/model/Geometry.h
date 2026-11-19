#pragma once

// Page geometry in Xournal points (1/72"). Doubles throughout: the file's numbers keep their precision when content
// is moved (floats would round them).

#include <algorithm>

namespace xournal
{
    struct Point
    {
        double x = 0, y = 0;
        bool operator==(Point const&) const = default;
    };

    struct Size
    {
        double width = 0, height = 0;
        bool operator==(Size const&) const = default;
    };

    struct Rect
    {
        double x = 0, y = 0, width = 0, height = 0;

        double Left() const { return x; }
        double Top() const { return y; }
        double Right() const { return x + width; }
        double Bottom() const { return y + height; }

        /// The rectangle spanned by two corners in any order.
        static Rect FromCorners(Point a, Point b)
        {
            double left = std::min(a.x, b.x), top = std::min(a.y, b.y);
            return { left, top, std::max(a.x, b.x) - left, std::max(a.y, b.y) - top };
        }

        bool operator==(Rect const&) const = default;
    };

    /// An affine transform in the row-vector convention of Windows (D2D1, System.Numerics, ink's PointTransform):
    /// x' = x m11 + y m21 + m31, y' = x m12 + y m22 + m32; `a * b` applies a, then b.
    struct Matrix
    {
        double m11 = 1, m12 = 0, m21 = 0, m22 = 1, m31 = 0, m32 = 0;

        static Matrix Translation(double x, double y) { return { 1, 0, 0, 1, x, y }; }
        static Matrix Scale(double sx, double sy) { return { sx, 0, 0, sy, 0, 0 }; }
        static Matrix Scale(double s, Point center) { return { s, 0, 0, s, center.x * (1 - s), center.y * (1 - s) }; }

        Point Apply(Point p) const { return { p.x * m11 + p.y * m21 + m31, p.x * m12 + p.y * m22 + m32 }; }
        Point Translation() const { return { m31, m32 }; }

        friend Matrix operator*(Matrix const& a, Matrix const& b)
        {
            return { a.m11 * b.m11 + a.m12 * b.m21, a.m11 * b.m12 + a.m12 * b.m22,
                     a.m21 * b.m11 + a.m22 * b.m21, a.m21 * b.m12 + a.m22 * b.m22,
                     a.m31 * b.m11 + a.m32 * b.m21 + b.m31, a.m31 * b.m12 + a.m32 * b.m22 + b.m32 };
        }

        bool operator==(Matrix const&) const = default;
    };
}
