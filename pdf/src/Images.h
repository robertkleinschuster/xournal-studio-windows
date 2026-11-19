#pragma once

// Internal: pictures as PDF image XObjects. Decoded with Windows Imaging Component and written as raw samples that the
// writer compresses with Flate (lossless), alpha as a soft mask. Baseline/progressive JPEG data in RGB or gray is passed
// through unchanged (DCTDecode) - never decoded and re-encoded.

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>

namespace xournal::pdf::detail
{
    class Images
    {
    public:
        explicit Images(QPDF& pdf);

        /// An image XObject (indirect) of the encoded picture; throws xournal::Error when WIC cannot decode it.
        QPDFObjectHandle FromData(std::span<std::uint8_t const> data);

    private:
        QPDF& pdf;
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    };
}
