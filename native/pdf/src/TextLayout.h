#pragma once

// Internal: lays out a text with DirectWrite (font matching and fallback, shaping, wrapping, alignment, justification)
// and returns its glyph runs - what DirectWrite would draw, as positioned glyphs of font faces.

#include "xournal/model/Geometry.h"
#include "xournal/model/XoppText.h"

#include <dwrite_3.h>
#include <wrl/client.h>

#include <set>
#include <string>
#include <vector>

namespace xournal::pdf::detail
{
    struct GlyphRun
    {
        Microsoft::WRL::ComPtr<IDWriteFontFace> face;
        float emSize = 0;
        bool sideways = false;
        bool rightToLeft = false;
        Point origin;                             // baseline origin in the text's box (points, y down)
        std::vector<UINT16> glyphs;
        std::vector<float> advances;              // points, in reading direction
        std::vector<DWRITE_GLYPH_OFFSET> offsets; // empty when all are zero
        std::vector<std::wstring> glyphText;      // the characters a glyph stands for (first glyph of a cluster)
    };

    class TextLayout
    {
    public:
        TextLayout();
        /// The text's box at the origin, as the screen lays it out: one line per paragraph aligned within the widest,
        /// or wrapped at its wrap width (justified if asked).
        std::vector<GlyphRun> Layout(XoppText const& text);

    private:
        Microsoft::WRL::ComPtr<IDWriteFactory> factory;
        Microsoft::WRL::ComPtr<IDWriteFontCollection> systemFonts;
        std::wstring locale;
        std::set<std::wstring> reportedMissing;
    };
}
