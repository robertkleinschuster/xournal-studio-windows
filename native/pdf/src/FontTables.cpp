#include "FontTables.h"

#include "xournal/base/Error.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace xournal::pdf::detail
{
    namespace
    {
        constexpr std::array<std::string_view, 11> PdfTables{ "OS/2", "cmap", "cvt ", "fpgm", "glyf", "head", "hhea",
                                                              "hmtx", "loca", "maxp", "prep" }; // sorted by tag

        std::uint32_t U32(std::string_view s, size_t at)
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at])) << 24 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 16 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 8 |
                   static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 3]));
        }

        std::uint16_t U16(std::string_view s, size_t at)
        {
            return static_cast<std::uint16_t>(static_cast<unsigned char>(s[at]) << 8 | static_cast<unsigned char>(s[at + 1]));
        }

        void Put16(std::string& s, std::uint16_t v)
        {
            s += static_cast<char>(v >> 8);
            s += static_cast<char>(v & 0xFF);
        }

        void Put32(std::string& s, std::uint32_t v)
        {
            Put16(s, static_cast<std::uint16_t>(v >> 16));
            Put16(s, static_cast<std::uint16_t>(v & 0xFFFF));
        }

        void Set32(std::string& s, size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
            {
                s[at + i] = static_cast<char>(v >> (24 - 8 * i));
            }
        }

        // Sum of the data as big-endian uint32s, padded with zeros (OpenType CalcTableChecksum).
        std::uint32_t Checksum(std::string_view data)
        {
            std::uint32_t sum = 0;
            for (size_t i = 0; i < data.size(); i += 4)
            {
                std::uint32_t word = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    word = word << 8 | (i + k < data.size() ? static_cast<unsigned char>(data[i + k]) : 0u);
                }
                sum += word;
            }
            return sum;
        }

        struct Table
        {
            std::string_view tag;
            std::string data;
        };
    }

    std::string KeepPdfTables(std::string_view font)
    {
        if (font.size() < 12 || (U32(font, 0) != 0x00010000 && font.substr(0, 4) != "true"))
        {
            ThrowInvalidData("embedded font: not a TrueType font file");
        }
        std::uint16_t count = U16(font, 4);
        if (font.size() < 12 + static_cast<size_t>(count) * 16)
        {
            ThrowInvalidData("embedded font: table directory cut off");
        }
        std::vector<Table> tables;
        for (std::uint16_t i = 0; i < count; ++i)
        {
            size_t record = 12 + static_cast<size_t>(i) * 16;
            std::string_view tag = font.substr(record, 4);
            if (!std::binary_search(PdfTables.begin(), PdfTables.end(), tag))
            {
                continue;
            }
            std::uint32_t offset = U32(font, record + 8), length = U32(font, record + 12);
            if (static_cast<std::uint64_t>(offset) + length > font.size())
            {
                ThrowInvalidData("embedded font: table beyond the end of the file");
            }
            tables.push_back({ PdfTables[std::lower_bound(PdfTables.begin(), PdfTables.end(), tag) - PdfTables.begin()],
                               std::string(font.substr(offset, length)) });
        }
        std::sort(tables.begin(), tables.end(), [](Table const& a, Table const& b) { return a.tag < b.tag; });

        auto n = static_cast<std::uint16_t>(tables.size());
        std::uint16_t entrySelector = 0;
        while ((2u << entrySelector) <= n) ++entrySelector;
        auto searchRange = static_cast<std::uint16_t>((1u << entrySelector) * 16);

        std::string out;
        Put32(out, U32(font, 0));
        Put16(out, n);
        Put16(out, searchRange);
        Put16(out, entrySelector);
        Put16(out, static_cast<std::uint16_t>(n * 16 - searchRange));
        size_t offset = 12 + static_cast<size_t>(n) * 16;
        size_t headAt = 0;
        for (auto& t : tables)
        {
            if (t.tag == "head" && t.data.size() >= 12)
            {
                Set32(t.data, 8, 0); // checkSumAdjustment is computed over the whole file below
            }
            out.append(t.tag);
            Put32(out, Checksum(t.data));
            Put32(out, static_cast<std::uint32_t>(offset));
            Put32(out, static_cast<std::uint32_t>(t.data.size()));
            if (t.tag == "head") headAt = offset;
            offset += (t.data.size() + 3) & ~size_t{ 3 };
        }
        for (auto const& t : tables)
        {
            out += t.data;
            out.append((4 - t.data.size() % 4) % 4, '\0');
        }
        if (headAt != 0)
        {
            Set32(out, headAt + 8, 0xB1B0AFBAu - Checksum(out));
        }
        return out;
    }
}
