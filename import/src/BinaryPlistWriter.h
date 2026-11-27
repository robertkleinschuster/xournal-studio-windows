#pragma once

// Internal: a writer of Apple's binary property lists ("bplist00", as NSKeyedArchiver writes them; the layout the
// reader (BinaryPlist) reads): objects are added one by one and refer to each other by the number Add* returns, then
// Finish(top) writes the header, the objects, the offset table and the trailer. References are two bytes wide (fewer
// than 65536 objects), offsets four.

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace xournal::import::detail
{
    class BinaryPlistWriter
    {
    public:
        using Ref = std::uint64_t;

        Ref Bool(bool value);
        Ref Int(std::int64_t value);
        Ref Real(double value);
        /// UTF-8 text: ASCII is written as such, anything else as UTF-16.
        Ref String(std::string_view utf8);
        Ref Data(std::span<std::uint8_t const> bytes);
        /// A reference to another object of an NSKeyedArchiver archive ($objects index).
        Ref Uid(std::uint64_t value);
        Ref Array(std::vector<Ref> const& items);
        Ref Dictionary(std::vector<std::pair<Ref, Ref>> const& entries);

        std::vector<std::uint8_t> Finish(Ref top) const;

    private:
        Ref Add(std::vector<std::uint8_t> object);

        std::vector<std::vector<std::uint8_t>> objects;
    };
}
