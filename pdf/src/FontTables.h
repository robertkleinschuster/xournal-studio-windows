#pragma once

// Internal: reduces a TrueType font file to the tables a PDF viewer uses for an embedded CIDFontType2 (ISO 32000-1,
// 9.9: glyf, head, hhea, hmtx, loca, maxp, cvt, fpgm, prep; plus cmap and OS/2, both small). Windows' subsetter keeps
// layout and device tables (GPOS, GSUB, hdmx, ...) a PDF never reads - most of a subset's size. The table directory is
// rebuilt with the tables' checksums and head.checkSumAdjustment (OpenType spec, "Table directory").

#include <string>
#include <string_view>

namespace xournal::pdf::detail
{
    /// The font with only the PDF tables (in tag order, 4-byte aligned). Throws xournal::Error for data that is not a
    /// TrueType font (sfnt version 0x00010000 or 'true').
    std::string KeepPdfTables(std::string_view font);
}
