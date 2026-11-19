#pragma once

// Internal: what a text box annotation's appearance draws - where its first line starts and how large (read from its
// appearance stream with qpdf's content parser), and the size and color of its /DA.

#include "xournal/model/Color.h"

#include <qpdf/QPDFObjectHandle.hh>

#include <optional>
#include <string_view>

namespace xournal::import::detail
{
    struct FirstLine
    {
        double x = 0, y = 0; // page space (PDF, y up)
        double size = 0;     // font size as drawn, 1 decimal
    };

    /// Where the normal appearance begins its text and its font size: the CTM and text matrix at the first
    /// text-showing operator, the form mapped onto the annotation's rectangle as a viewer does (BBox -> Rect).
    /// Nothing without an appearance or text, or for one that cannot be parsed (logged).
    std::optional<FirstLine> FirstLineOf(QPDFObjectHandle annotation);

    struct DefaultAppearance
    {
        double size = 12;
        Color color = Colors::Black;
    };

    /// "/Helvetica 12 Tf 0 g" or "... 1 0 0 rg": the font size and the fill color (a component that is no number
    /// counts as 0).
    DefaultAppearance ParseDefaultAppearance(std::string_view da);
}
