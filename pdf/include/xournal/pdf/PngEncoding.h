#pragma once

#include <wincodec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace xournal::pdf
{
    // Convert tightly packed premultiplied BGRA pixels to PNG through WIC.
    std::vector<std::uint8_t> EncodePng(IWICImagingFactory& factory, UINT width, UINT height,
        std::span<std::uint8_t const> pixels);
}
