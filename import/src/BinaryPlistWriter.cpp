#include "BinaryPlistWriter.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"

#include <bit>
#include <cstring>

namespace xournal::import::detail
{
    namespace
    {
        void Big(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
        {
            for (int i = bytes - 1; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }

        // The marker byte of an object with a length: the length in the low nibble, else 0xF and an integer object.
        void Head(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint64_t length)
        {
            if (length < 15)
            {
                out.push_back(static_cast<std::uint8_t>(type | length));
                return;
            }
            out.push_back(static_cast<std::uint8_t>(type | 0xF));
            out.push_back(0x12); // a four-byte integer
            Big(out, length, 4);
        }
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Add(std::vector<std::uint8_t> object)
    {
        if (objects.size() >= 0xFFFF) ThrowInvalidData("binary plist: too many objects");
        objects.push_back(std::move(object));
        return objects.size() - 1;
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Bool(bool value) { return Add({ static_cast<std::uint8_t>(value ? 0x09 : 0x08) }); }

    BinaryPlistWriter::Ref BinaryPlistWriter::Int(std::int64_t value)
    {
        std::vector<std::uint8_t> o;
        if (value >= 0 && value < 256) { o.push_back(0x10); Big(o, static_cast<std::uint64_t>(value), 1); }
        else if (value >= 0 && value < 65536) { o.push_back(0x11); Big(o, static_cast<std::uint64_t>(value), 2); }
        else if (value >= 0 && value <= 0xFFFFFFFFll) { o.push_back(0x12); Big(o, static_cast<std::uint64_t>(value), 4); }
        else { o.push_back(0x13); Big(o, static_cast<std::uint64_t>(value), 8); }
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Real(double value)
    {
        std::vector<std::uint8_t> o{ 0x23 };
        Big(o, std::bit_cast<std::uint64_t>(value), 8);
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::String(std::string_view utf8)
    {
        std::vector<std::uint8_t> o;
        bool ascii = true;
        for (char c : utf8) ascii = ascii && static_cast<unsigned char>(c) < 0x80;
        if (ascii)
        {
            Head(o, 0x50, utf8.size());
            o.insert(o.end(), utf8.begin(), utf8.end());
        }
        else
        {
            std::wstring wide = FromUtf8(std::string(utf8));
            Head(o, 0x60, wide.size());
            for (wchar_t c : wide) Big(o, static_cast<std::uint16_t>(c), 2);
        }
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Data(std::span<std::uint8_t const> bytes)
    {
        std::vector<std::uint8_t> o;
        Head(o, 0x40, bytes.size());
        o.insert(o.end(), bytes.begin(), bytes.end());
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Uid(std::uint64_t value)
    {
        std::vector<std::uint8_t> o;
        int bytes = value < 256 ? 1 : value < 65536 ? 2 : 4;
        o.push_back(static_cast<std::uint8_t>(0x80 | (bytes - 1)));
        Big(o, value, bytes);
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Array(std::vector<Ref> const& items)
    {
        std::vector<std::uint8_t> o;
        Head(o, 0xA0, items.size());
        for (Ref item : items) Big(o, item, 2);
        return Add(std::move(o));
    }

    BinaryPlistWriter::Ref BinaryPlistWriter::Dictionary(std::vector<std::pair<Ref, Ref>> const& entries)
    {
        std::vector<std::uint8_t> o;
        Head(o, 0xD0, entries.size());
        for (auto const& [key, value] : entries) Big(o, key, 2);
        for (auto const& [key, value] : entries) Big(o, value, 2);
        return Add(std::move(o));
    }

    std::vector<std::uint8_t> BinaryPlistWriter::Finish(Ref top) const
    {
        std::vector<std::uint8_t> out{ 'b', 'p', 'l', 'i', 's', 't', '0', '0' };
        std::vector<std::uint32_t> offsets;
        for (auto const& object : objects)
        {
            offsets.push_back(static_cast<std::uint32_t>(out.size()));
            out.insert(out.end(), object.begin(), object.end());
        }
        auto table = static_cast<std::uint64_t>(out.size());
        for (auto offset : offsets) Big(out, offset, 4);
        out.insert(out.end(), 6, 0); // unused, sort version
        out.push_back(4);            // offset size
        out.push_back(2);            // reference size
        Big(out, objects.size(), 8);
        Big(out, top, 8);
        Big(out, table, 8);
        return out;
    }
}
