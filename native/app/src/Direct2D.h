#pragma once

// The model's geometry and colors as Direct2D takes them.

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"

#include <d2d1_1.h>

namespace xournal::app
{
    inline D2D1_MATRIX_3X2_F ToD2D(Matrix const& m)
    {
        return D2D1::Matrix3x2F(static_cast<float>(m.m11), static_cast<float>(m.m12), static_cast<float>(m.m21),
            static_cast<float>(m.m22), static_cast<float>(m.m31), static_cast<float>(m.m32));
    }

    inline D2D1_COLOR_F ToD2D(Color c)
    {
        return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    }

    inline D2D1_POINT_2F ToD2D(Point p) { return { static_cast<float>(p.x), static_cast<float>(p.y) }; }
}
