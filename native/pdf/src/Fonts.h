#pragma once

// Internal: the texts' fonts as embedded subsets. A TrueType face (also in a collection) without simulations whose
// license allows embedding (OS/2 fsType) is subset with Windows' Font Subsetting API (CreateFontPackage, glyph list; it
// keeps the glyph ids), reduced to the tables a PDF uses (FontTables) and written as a Type0 font (CIDFontType2,
// Identity-H, glyph ids as CIDs) with widths and a ToUnicode map - real, searchable text. Other faces (CFF outlines,
// restricted licenses, simulated bold/oblique, failed subsets) are drawn as outlines.
//
// Use: Collect() every glyph run first, Build() once, then For() while writing the pages.

#include "TextLayout.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xournal::pdf::detail
{
    struct EmbeddedFont
    {
        QPDFObjectHandle dictionary;       // the Type0 font (indirect)
        std::map<UINT16, double> widths;   // glyph id -> advance in 1/1000 em, as in /W
    };

    class Fonts
    {
    public:
        explicit Fonts(QPDF& pdf) : pdf(pdf) {}

        void Collect(GlyphRun const& run);
        void Build();
        /// The embedded font of a face, or null when it is drawn as outlines.
        EmbeddedFont const* For(IDWriteFontFace* face) const;

    private:
        struct Face
        {
            Microsoft::WRL::ComPtr<IDWriteFontFace> face;
            std::map<UINT16, std::wstring> glyphs; // used glyphs -> text (ToUnicode)
            std::optional<EmbeddedFont> font;
        };

        std::string KeyOf(IDWriteFontFace* face) const;
        EmbeddedFont Embed(Face& face, size_t number);

        QPDF& pdf;
        std::map<std::string, Face> faces;
        mutable std::map<IDWriteFontFace*, std::string> keys;
    };

    /// Why a face cannot be embedded as a TrueType subset, or nothing when it can.
    std::optional<std::string> WhyNotEmbeddable(IDWriteFontFace* face);

    /// A face's font file (the whole file; for a collection all of its faces).
    std::vector<std::uint8_t> FontFileData(IDWriteFontFace* face);
}
