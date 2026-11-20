#include "Bitmaps.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/io/Files.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

using Microsoft::WRL::ComPtr;

namespace xournal::app
{
    namespace Bitmaps
    {
        namespace
        {
            ComPtr<IWICImagingFactory> Factory()
            {
                ComPtr<IWICImagingFactory> factory;
                Check("CoCreateInstance(WICImagingFactory)",
                    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
                return factory;
            }
        }

        std::shared_ptr<CpuBitmap> Decode(std::span<std::uint8_t const> data)
        {
            auto factory = Factory();
            ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream(data.data(), static_cast<UINT>(data.size())));
            if (!stream)
            {
                ThrowHr("SHCreateMemStream (picture)", E_OUTOFMEMORY);
            }
            ComPtr<IWICBitmapDecoder> decoder;
            Check("IWICImagingFactory::CreateDecoderFromStream",
                factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame;
            Check("IWICBitmapDecoder::GetFrame", decoder->GetFrame(0, &frame));
            ComPtr<IWICFormatConverter> converter;
            Check("IWICImagingFactory::CreateFormatConverter", factory->CreateFormatConverter(&converter));
            Check("IWICFormatConverter::Initialize", converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            auto bitmap = std::make_shared<CpuBitmap>();
            Check("IWICBitmapSource::GetSize", converter->GetSize(&bitmap->width, &bitmap->height));
            bitmap->pixels.resize(static_cast<size_t>(bitmap->width) * bitmap->height * 4);
            Check("IWICBitmapSource::CopyPixels", converter->CopyPixels(nullptr, bitmap->width * 4,
                static_cast<UINT>(bitmap->pixels.size()), bitmap->pixels.data()));
            return bitmap;
        }

        std::vector<std::uint8_t> EncodePng(CpuBitmap const& bitmap)
        {
            auto factory = Factory();
            UINT stride = bitmap.width * 4;
            ComPtr<IWICBitmap> source;
            Check("IWICImagingFactory::CreateBitmapFromMemory", factory->CreateBitmapFromMemory(bitmap.width, bitmap.height,
                GUID_WICPixelFormat32bppPBGRA, stride, static_cast<UINT>(bitmap.pixels.size()),
                const_cast<BYTE*>(bitmap.pixels.data()), &source));
            ComPtr<IWICFormatConverter> converter; // PNG keeps straight alpha
            Check("IWICImagingFactory::CreateFormatConverter", factory->CreateFormatConverter(&converter));
            Check("IWICFormatConverter::Initialize", converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
            ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream(nullptr, 0));
            if (!stream) ThrowHr("SHCreateMemStream (PNG)", E_OUTOFMEMORY);
            ComPtr<IWICBitmapEncoder> encoder;
            Check("IWICImagingFactory::CreateEncoder (PNG)", factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
            Check("IWICBitmapEncoder::Initialize", encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
            ComPtr<IWICBitmapFrameEncode> frame;
            Check("IWICBitmapEncoder::CreateNewFrame", encoder->CreateNewFrame(&frame, nullptr));
            Check("IWICBitmapFrameEncode::Initialize", frame->Initialize(nullptr));
            Check("IWICBitmapFrameEncode::WriteSource", frame->WriteSource(converter.Get(), nullptr));
            Check("IWICBitmapFrameEncode::Commit", frame->Commit());
            Check("IWICBitmapEncoder::Commit", encoder->Commit());
            ULARGE_INTEGER size{};
            Check("IStream_Size", IStream_Size(stream.Get(), &size));
            Check("IStream_Reset", IStream_Reset(stream.Get()));
            std::vector<std::uint8_t> png(static_cast<size_t>(size.QuadPart));
            Check("IStream_Read", IStream_Read(stream.Get(), png.data(), static_cast<ULONG>(png.size())));
            return png;
        }

        std::shared_ptr<CpuBitmap> DecodeFile(std::filesystem::path const& file)
        {
            auto data = io::Files::ReadAll(file);
            return Decode(data);
        }

        std::shared_ptr<CpuBitmap> Downsample(CpuBitmap const& bitmap, UINT width, UINT height)
        {
            auto factory = Factory();
            ComPtr<IWICBitmap> source;
            Check("IWICImagingFactory::CreateBitmapFromMemory (thumbnail)", factory->CreateBitmapFromMemory(
                bitmap.width, bitmap.height, GUID_WICPixelFormat32bppPBGRA, bitmap.width * 4,
                static_cast<UINT>(bitmap.pixels.size()), const_cast<BYTE*>(bitmap.pixels.data()), &source));
            ComPtr<IWICBitmapScaler> scaler;
            Check("IWICImagingFactory::CreateBitmapScaler", factory->CreateBitmapScaler(&scaler));
            Check("IWICBitmapScaler::Initialize", scaler->Initialize(source.Get(), width, height, WICBitmapInterpolationModeFant));
            auto result = std::make_shared<CpuBitmap>();
            result->width = width;
            result->height = height;
            result->pixels.resize(static_cast<std::size_t>(width) * height * 4);
            Check("IWICBitmapScaler::CopyPixels", scaler->CopyPixels(nullptr, width * 4,
                static_cast<UINT>(result->pixels.size()), result->pixels.data()));
            return result;
        }

        winrt::Windows::Data::Pdf::PdfDocument LoadPdf(std::span<std::uint8_t const> data)
        {
            using namespace winrt::Windows::Storage::Streams;
            InMemoryRandomAccessStream stream;
            DataWriter writer(stream);
            writer.WriteBytes(winrt::array_view<std::uint8_t const>(data.data(), data.data() + data.size()));
            writer.StoreAsync().get();
            writer.DetachStream();
            stream.Seek(0);
            return winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(stream).get();
        }
    }

    PixelSize FitPixels(double width, double height, double pixelsPerUnit, UINT maxWidth)
    {
        constexpr double MaxSide = 16384; // D3D11 textures and Direct2D bitmaps
        if (!(width > 0) || !(height > 0) || !(pixelsPerUnit > 0)) return { 1, 1 };
        double w = std::min({ width * pixelsPerUnit, static_cast<double>(maxWidth), MaxSide });
        double h = w * height / width;
        if (h > MaxSide)
        {
            h = MaxSide;
            w = h * width / height;
        }
        return { std::max<UINT>(1, static_cast<UINT>(std::ceil(w))), std::max<UINT>(1, static_cast<UINT>(std::ceil(h))) };
    }

    ID2D1Bitmap* DrawableBitmap::On(ID2D1DeviceContext* dc)
    {
        ComPtr<ID2D1Device> current;
        dc->GetDevice(&current);
        if (!uploaded || current != device)
        {
            if (!pixels) return nullptr; // let go after the upload; the device changed since
            D2D1_BITMAP_PROPERTIES properties{ { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96, 96 };
            uploaded.Reset();
            auto start = std::chrono::steady_clock::now();
            Check("ID2D1DeviceContext::CreateBitmap (underlay)", dc->CreateBitmap({ pixels->width, pixels->height },
                pixels->pixels.data(), pixels->width * 4, properties, &uploaded));
            device = current;
            Log(std::format("bitmap upload: {}x{} px in {} ms", pixels->width, pixels->height,
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
            if (!keepPixels) pixels.reset();
        }
        return uploaded.Get();
    }
}
