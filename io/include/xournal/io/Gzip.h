#pragma once

// gzip, as Xournal++ stores .xopp files (zlib: Windows has no public gzip API).

#include <cstdint>
#include <span>
#include <vector>

namespace xournal::io::Gzip
{
    /// Starts with the gzip magic bytes (1f 8b); otherwise a .xopp is plain XML.
    bool IsGzip(std::span<std::uint8_t const> data);

    /// The uncompressed data (all members of a multi-member file). Throws on damaged data.
    std::vector<std::uint8_t> Decompress(std::span<std::uint8_t const> data);

    /// gzip at the fastest level, as the C# app wrote (saving often, recovery copies every few seconds).
    std::vector<std::uint8_t> Compress(std::span<std::uint8_t const> data);
}
