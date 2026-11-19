#pragma once

// Internal: writes a laid-out text into a page's content - glyph runs of embedded fonts as PDF text (TJ with the
// layout's advances), the others as filled outlines (IDWriteFontFace::GetGlyphRunOutline). Page space: y down.

#include "ContentStream.h"
#include "Fonts.h"
#include "PageResources.h"
#include "TextLayout.h"

#include <vector>

namespace xournal::pdf::detail
{
    /// Returns the number of runs drawn as outlines.
    int DrawText(ContentStream& content, PageResources& resources, AlphaStates& alpha, Fonts const& fonts,
                 XoppText const& text, std::vector<GlyphRun> const& runs);
}
