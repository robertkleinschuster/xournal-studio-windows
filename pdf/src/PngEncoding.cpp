#include "xournal/pdf/PngEncoding.h"

#include "xournal/base/Error.h"

#include <climits>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf
{
    std::vector<std::uint8_t> EncodePng(IWICImagingFactory& factory, UINT width, UINT height,
        std::span<std::uint8_t const> pixels)
    {
        if (width == 0 || height == 0 || width > UINT_MAX / 4 || height > UINT_MAX / (width * 4) ||
            pixels.size() != static_cast<size_t>(width) * height * 4)
            ThrowHr("EncodePng (BGRA dimensions)", E_INVALIDARG);

        ComPtr<IWICBitmap> bitmap;
        Check("IWICImagingFactory::CreateBitmapFromMemory", factory.CreateBitmapFromMemory(width, height,
            GUID_WICPixelFormat32bppPBGRA, width * 4, static_cast<UINT>(pixels.size()),
            const_cast<BYTE*>(pixels.data()), &bitmap));
        ComPtr<IWICFormatConverter> converter;
        Check("IWICImagingFactory::CreateFormatConverter", factory.CreateFormatConverter(&converter));
        Check("IWICFormatConverter::Initialize", converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
        ComPtr<IStream> stream;
        Check("CreateStreamOnHGlobal (PNG)", CreateStreamOnHGlobal(nullptr, TRUE, &stream));
        ComPtr<IWICBitmapEncoder> encoder;
        Check("IWICImagingFactory::CreateEncoder (PNG)", factory.CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        Check("IWICBitmapEncoder::Initialize", encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;
        Check("IWICBitmapEncoder::CreateNewFrame", encoder->CreateNewFrame(&frame, nullptr));
        Check("IWICBitmapFrameEncode::Initialize", frame->Initialize(nullptr));
        Check("IWICBitmapFrameEncode::WriteSource", frame->WriteSource(converter.Get(), nullptr));
        Check("IWICBitmapFrameEncode::Commit", frame->Commit());
        Check("IWICBitmapEncoder::Commit", encoder->Commit());

        STATSTG stat{};
        Check("IStream::Stat (PNG)", stream->Stat(&stat, STATFLAG_NONAME));
        if (stat.cbSize.QuadPart > ULONG_MAX) ThrowHr("PNG stream size", E_OUTOFMEMORY);
        std::vector<std::uint8_t> png(static_cast<size_t>(stat.cbSize.QuadPart));
        Check("IStream::Seek (PNG)", stream->Seek({}, STREAM_SEEK_SET, nullptr));
        ULONG read = 0;
        Check("IStream::Read (PNG)", stream->Read(png.data(), static_cast<ULONG>(png.size()), &read));
        if (read != png.size()) ThrowHr("IStream::Read (incomplete PNG)", STG_E_READFAULT);
        return png;
    }
}
