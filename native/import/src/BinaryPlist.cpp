#include "BinaryPlist.h"

#include "xournal/base/Error.h"

#include <bit>
#include <cstring>
#include <format>

namespace xournal::import::detail
{
    namespace
    {
        constexpr size_t TrailerSize = 32;
    }

    BinaryPlist::BinaryPlist(std::span<std::uint8_t const> bytes) : data(bytes.begin(), bytes.end())
    {
        if (data.size() < 8 + TrailerSize || std::memcmp(data.data(), "bplist0", 7) != 0)
        {
            ThrowInvalidData("binary plist: no bplist0 header");
        }
        size_t t = data.size() - TrailerSize;
        offsetSize = data[t + 6];
        refSize = data[t + 7];
        count = Read(t + 8, 8);
        top = Read(t + 16, 8);
        std::uint64_t table = Read(t + 24, 8);
        if (offsetSize < 1 || offsetSize > 8 || refSize < 1 || refSize > 8 || top >= count || table >= t ||
            count > (t - table) / offsetSize)
        {
            ThrowInvalidData("binary plist: damaged trailer");
        }
        tableAt = static_cast<size_t>(table);
    }

    std::uint64_t BinaryPlist::Read(size_t at, size_t size) const
    {
        if (at > data.size() || size > data.size() - at)
        {
            ThrowInvalidData("binary plist: read beyond the end");
        }
        std::uint64_t v = 0;
        for (size_t i = 0; i < size; ++i) v = v << 8 | data[at + i];
        return v;
    }

    std::pair<std::uint64_t, size_t> BinaryPlist::Length(size_t at, std::uint8_t low) const
    {
        if (low != 0x0F) return { low, at + 1 };
        // The length follows as an integer object (marker 0x1n: 2^n bytes).
        std::uint8_t marker = static_cast<std::uint8_t>(Read(at + 1, 1));
        if ((marker & 0xF0) != 0x10 || (marker & 0x0F) > 3)
        {
            ThrowInvalidData("binary plist: damaged length");
        }
        size_t size = size_t{ 1 } << (marker & 0x0F);
        return { Read(at + 2, size), at + 2 + size };
    }

    BinaryPlist::Value BinaryPlist::Object(std::uint64_t number) const
    {
        if (number >= count)
        {
            ThrowInvalidData(std::format("binary plist: object {} of {}", number, count));
        }
        size_t at = static_cast<size_t>(Read(tableAt + static_cast<size_t>(number) * offsetSize, offsetSize));
        std::uint8_t marker = static_cast<std::uint8_t>(Read(at, 1));
        std::uint8_t high = marker >> 4, low = marker & 0x0F;
        auto checked = [&](std::uint64_t length, size_t start, std::uint64_t unit) {
            if (length > (data.size() - std::min(start, data.size())) / unit)
            {
                ThrowInvalidData("binary plist: object beyond the end");
            }
            return static_cast<size_t>(length);
        };
        switch (high)
        {
        case 0x0:
            if (marker == 0x08) return false;
            if (marker == 0x09) return true;
            return std::monostate{};
        case 0x1:
        {
            if (low > 3) ThrowInvalidData("binary plist: integer wider than 8 bytes");
            size_t size = size_t{ 1 } << low;
            std::uint64_t v = Read(at + 1, size);
            if (size < 8) return static_cast<std::int64_t>(v); // shorter integers are unsigned
            return static_cast<std::int64_t>(v);
        }
        case 0x2:
        {
            if (low == 2) return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(Read(at + 1, 4))));
            if (low == 3) return std::bit_cast<double>(Read(at + 1, 8));
            ThrowInvalidData("binary plist: real of an unknown size");
        }
        case 0x3:
            return std::bit_cast<double>(Read(at + 1, 8)); // date: seconds since 2001
        case 0x4:
        {
            auto [length, start] = Length(at, low);
            size_t n = checked(length, start, 1);
            Read(start, n); // bounds
            return Bytes(data.begin() + static_cast<std::ptrdiff_t>(start), data.begin() + static_cast<std::ptrdiff_t>(start + n));
        }
        case 0x5:
        {
            auto [length, start] = Length(at, low);
            size_t n = checked(length, start, 1);
            Read(start, n);
            std::wstring s;
            for (size_t i = 0; i < n; ++i) s += static_cast<wchar_t>(data[start + i]);
            return s;
        }
        case 0x6:
        {
            auto [length, start] = Length(at, low);
            size_t n = checked(length, start, 2);
            std::wstring s;
            for (size_t i = 0; i < n; ++i) s += static_cast<wchar_t>(Read(start + 2 * i, 2));
            return s;
        }
        case 0x8:
            return Uid{ Read(at + 1, size_t{ low } + 1) };
        case 0xA:
        {
            auto [length, start] = Length(at, low);
            size_t n = checked(length, start, refSize);
            Array a;
            for (size_t i = 0; i < n; ++i) a.items.push_back(Read(start + i * refSize, refSize));
            return a;
        }
        case 0xD:
        {
            auto [length, start] = Length(at, low);
            size_t n = checked(length, start, 2 * refSize);
            Dictionary d;
            for (size_t i = 0; i < n; ++i)
            {
                d.entries.emplace_back(Read(start + i * refSize, refSize), Read(start + (n + i) * refSize, refSize));
            }
            return d;
        }
        default:
            ThrowInvalidData(std::format("binary plist: unknown object marker 0x{:02x}", marker));
        }
    }

    std::optional<std::uint64_t> BinaryPlist::Find(Dictionary const& d, std::wstring_view key) const
    {
        for (auto const& [k, v] : d.entries)
        {
            auto name = Object(k);
            if (auto const* s = std::get_if<std::wstring>(&name); s && *s == key) return v;
        }
        return std::nullopt;
    }
}
