#include "xournal/io/Gzip.h"

#include "xournal/base/Error.h"

#include <zlib.h>

#include <format>
#include <limits>

namespace xournal::io::Gzip
{
    namespace
    {
        constexpr int GzipWindowBits = 15 + 16; // zlib: 15-bit window, gzip header and trailer
        constexpr size_t Chunk = 256 * 1024;

        [[noreturn]] void ThrowZlib(char const* step, int code, z_stream const& stream)
        {
            ThrowInvalidData(std::format("zlib {}: {} {}", step, code, stream.msg ? stream.msg : ""));
        }

        // zlib counts in uInt; .xopp files are far below 4 GB, but a larger one must fail, not wrap around.
        uInt Size(size_t size)
        {
            if (size > std::numeric_limits<uInt>::max())
            {
                ThrowInvalidData("gzip: data larger than 4 GB");
            }
            return static_cast<uInt>(size);
        }
    }

    bool IsGzip(std::span<std::uint8_t const> data)
    {
        return data.size() >= 2 && data[0] == 0x1F && data[1] == 0x8B;
    }

    std::vector<std::uint8_t> Decompress(std::span<std::uint8_t const> data)
    {
        z_stream stream{};
        int code = inflateInit2(&stream, GzipWindowBits);
        if (code != Z_OK)
        {
            ThrowZlib("inflateInit2", code, stream);
        }
        std::vector<std::uint8_t> output;
        stream.next_in = const_cast<Bytef*>(data.data());
        stream.avail_in = Size(data.size());
        for (;;)
        {
            size_t used = output.size();
            output.resize(used + Chunk);
            stream.next_out = output.data() + used;
            stream.avail_out = static_cast<uInt>(Chunk);
            code = inflate(&stream, Z_NO_FLUSH);
            output.resize(used + Chunk - stream.avail_out);
            if (code == Z_STREAM_END)
            {
                if (stream.avail_in == 0)
                {
                    break;
                }
                code = inflateReset(&stream); // another gzip member follows
            }
            if (code == Z_BUF_ERROR && stream.avail_in == 0)
            {
                inflateEnd(&stream);
                ThrowInvalidData("gzip: the data ends early (truncated file)");
            }
            if (code != Z_OK)
            {
                z_stream failed = stream;
                inflateEnd(&stream);
                ThrowZlib("inflate", code, failed);
            }
        }
        inflateEnd(&stream);
        return output;
    }

    std::vector<std::uint8_t> Compress(std::span<std::uint8_t const> data)
    {
        z_stream stream{};
        int code = deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, GzipWindowBits, 8, Z_DEFAULT_STRATEGY);
        if (code != Z_OK)
        {
            ThrowZlib("deflateInit2", code, stream);
        }
        std::vector<std::uint8_t> output(deflateBound(&stream, Size(data.size())));
        stream.next_in = const_cast<Bytef*>(data.data());
        stream.avail_in = Size(data.size());
        stream.next_out = output.data();
        stream.avail_out = Size(output.size());
        code = deflate(&stream, Z_FINISH); // the bound guarantees one call finishes
        if (code != Z_STREAM_END)
        {
            z_stream failed = stream;
            deflateEnd(&stream);
            ThrowZlib("deflate", code, failed);
        }
        output.resize(stream.total_out);
        deflateEnd(&stream);
        return output;
    }
}
