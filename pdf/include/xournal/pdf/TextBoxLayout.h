#pragma once

// The DirectWrite layout of a .xopp text's box, the one rule the export draws by and the search measures hits with:
// the text's family (a Windows family; DirectWrite's fallback covers missing ones and glyphs), weight, style and size,
// one line per paragraph aligned within the widest line, or wrapped at its wrap width (justified if asked). The box's
// origin is the layout's (0, 0); the text's transform places it on the page.

#include "xournal/model/XoppText.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>

namespace xournal::pdf
{
    /// `family`: the text's Windows family (XoppText::WindowsFamily(), Arial when empty); `locale` for shaping.
    /// Throws xournal::Error.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> TextBoxLayout(IDWriteFactory* factory, XoppText const& text,
                                                            std::wstring const& family, std::wstring const& locale);
}
