#pragma once

// Internal: a reader of Apple's binary property lists ("bplist00", the format NSKeyedArchiver writes; layout as in
// Apple's CFBinaryPList.c and plist-cil): an offset table, a trailer, objects addressed by number. Objects are decoded
// on demand; every offset, length and reference is checked, a damaged list throws (xournal::Error, invalid data).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xournal::import::detail
{
    class BinaryPlist
    {
    public:
        struct Uid { std::uint64_t value = 0; };
        struct Array { std::vector<std::uint64_t> items; };                               // object numbers
        struct Dictionary { std::vector<std::pair<std::uint64_t, std::uint64_t>> entries; }; // key, value numbers
        using Bytes = std::vector<std::uint8_t>;
        using Value = std::variant<std::monostate, bool, std::int64_t, double, Bytes, std::wstring, Uid, Array, Dictionary>;

        /// Checks the header and trailer; throws for data that is no binary plist.
        explicit BinaryPlist(std::span<std::uint8_t const> data);

        std::uint64_t Top() const { return top; }
        std::uint64_t Count() const { return count; }
        Value Object(std::uint64_t number) const;

        /// A dictionary's value for a string key, or nothing.
        std::optional<std::uint64_t> Find(Dictionary const& d, std::wstring_view key) const;

    private:
        std::uint64_t Read(size_t at, size_t size) const;
        std::pair<std::uint64_t, size_t> Length(size_t at, std::uint8_t low) const; // (length, offset of the data)

        std::vector<std::uint8_t> data;
        size_t offsetSize = 0, refSize = 0, tableAt = 0;
        std::uint64_t count = 0, top = 0;
    };
}
