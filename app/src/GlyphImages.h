#pragma once

// Images of Segoe Fluent Icons glyphs (Windows 11's icon font) for comctl32 controls: a 32-bit image list, one image per
// glyph, drawn in the button text color at the given pixel size. comctl32 draws the disabled look from the alpha.

#include <windows.h>
#include <commctrl.h>

#include <initializer_list>

namespace xournal::app
{
    /// One size x size image per glyph (in order). Throws when the list cannot be made.
    HIMAGELIST CreateGlyphImages(std::initializer_list<wchar_t> glyphs, int size);
}
