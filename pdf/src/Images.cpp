#include "Images.h"

#include "xournal/base/Error.h"

#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf::detail
{
    Images::Images(QPDF& pdf) : pdf(pdf)
    {
        Check("CoCreateInstance(WICImagingFactory)", CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
    }

    QPDFObjectHandle Images::FromData(std::span<std::uint8_t const> data)
    {
        if (data.empty())
        {
            ThrowInvalidData("picture without data");
        }
        ComPtr<IWICStream> stream;
        Check("IWICImagingFactory::CreateStream", wic->CreateStream(&stream));
        // WIC reads the caller's buffer (not copied); it outlives the decoder, both local to this call.
        Check("IWICStream::InitializeFromMemory", stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size())));
        ComPtr<IWICBitmapDecoder> decoder;
        Check("decode picture", wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
        ComPtr<IWICBitmapFrameDecode> frame;
        Check("IWICBitmapDecoder::GetFrame", decoder->GetFrame(0, &frame));
        UINT width = 0, height = 0;
        Check("IWICBitmapFrameDecode::GetSize", frame->GetSize(&width, &height));
        if (width == 0 || height == 0)
        {
            ThrowInvalidData("picture of size 0");
        }
        GUID container{};
        Check("GetContainerFormat", decoder->GetContainerFormat(&container));
        WICPixelFormatGUID format{};
        Check("GetPixelFormat", frame->GetPixelFormat(&format));

        auto image = pdf.newStream();
        auto dict = image.getDict();
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
        dict.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
        dict.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
        dict.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

        // JPEG in RGB or gray: the data as it is (no generation loss).
        bool jpegRgb = container == GUID_ContainerFormatJpeg && format == GUID_WICPixelFormat24bppBGR;
        bool jpegGray = container == GUID_ContainerFormatJpeg && format == GUID_WICPixelFormat8bppGray;
        if (jpegRgb || jpegGray)
        {
            dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName(jpegRgb ? "/DeviceRGB" : "/DeviceGray"));
            image.replaceStreamData(std::string(reinterpret_cast<char const*>(data.data()), data.size()),
                                    QPDFObjectHandle::newName("/DCTDecode"), QPDFObjectHandle::newNull());
            return image;
        }

        // Everything else: decoded to 8-bit RGBA (straight alpha); RGB samples + an alpha soft mask when not opaque.
        ComPtr<IWICFormatConverter> converter;
        Check("CreateFormatConverter", wic->CreateFormatConverter(&converter));
        Check("IWICFormatConverter::Initialize", converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                                                        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
        UINT const stride = width * 4;
        std::vector<BYTE> rgba(static_cast<size_t>(stride) * height);
        Check("CopyPixels", converter->CopyPixels(nullptr, stride, static_cast<UINT>(rgba.size()), rgba.data()));

        size_t const pixels = static_cast<size_t>(width) * height;
        std::string rgb(pixels * 3, '\0');
        std::string alpha(pixels, '\0');
        bool opaque = true;
        for (size_t i = 0; i < pixels; ++i)
        {
            rgb[i * 3] = static_cast<char>(rgba[i * 4]);
            rgb[i * 3 + 1] = static_cast<char>(rgba[i * 4 + 1]);
            rgb[i * 3 + 2] = static_cast<char>(rgba[i * 4 + 2]);
            alpha[i] = static_cast<char>(rgba[i * 4 + 3]);
            opaque = opaque && rgba[i * 4 + 3] == 255;
        }
        dict.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceRGB"));
        image.replaceStreamData(rgb, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull()); // Flate on write
        if (!opaque)
        {
            auto mask = pdf.newStream(alpha);
            auto m = mask.getDict();
            m.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
            m.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
            m.replaceKey("/Width", QPDFObjectHandle::newInteger(width));
            m.replaceKey("/Height", QPDFObjectHandle::newInteger(height));
            m.replaceKey("/ColorSpace", QPDFObjectHandle::newName("/DeviceGray"));
            m.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
            dict.replaceKey("/SMask", mask);
        }
        return image;
    }
}
