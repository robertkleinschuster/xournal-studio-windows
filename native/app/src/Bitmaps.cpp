#include "Bitmaps.h"

#include "xournal/base/Error.h"
#include "xournal/io/Files.h"

#include <shlwapi.h>
#include <wincodec.h>

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

        std::shared_ptr<CpuBitmap> DecodeFile(std::filesystem::path const& file)
        {
            auto data = io::Files::ReadAll(file);
            return Decode(data);
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

    ID2D1Bitmap* DrawableBitmap::On(ID2D1DeviceContext* dc)
    {
        ComPtr<ID2D1Device> current;
        dc->GetDevice(&current);
        if (!uploaded || current != device)
        {
            D2D1_BITMAP_PROPERTIES properties{ { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED }, 96, 96 };
            uploaded.Reset();
            Check("ID2D1DeviceContext::CreateBitmap (underlay)", dc->CreateBitmap({ pixels->width, pixels->height },
                pixels->pixels.data(), pixels->width * 4, properties, &uploaded));
            device = current;
        }
        return uploaded.Get();
    }
}
