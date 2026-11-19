#include "xournal/pdf/PdfAreaRenderer.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <windows.data.pdf.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf
{
    struct PdfAreaRenderer::Impl
    {
        winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IPdfRendererNative> renderer;
        ComPtr<IWICImagingFactory> wic;
        bool comInitialized = false;

        ~Impl()
        {
            renderer.Reset();
            context.Reset();
            device.Reset();
            wic.Reset();
            if (comInitialized) CoUninitialize();
        }
    };

    PdfAreaRenderer::PdfAreaRenderer(std::filesystem::path const& pdf) : impl(std::make_unique<Impl>())
    {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (com != RPC_E_CHANGED_MODE)
        {
            Check("CoInitializeEx", com);
            impl->comInitialized = true;
        }
        try
        {
            auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(Paths::Full(pdf).wstring()).get();
            impl->document = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        }
        catch (winrt::hresult_error const& e)
        {
            ThrowHr(std::format("load {} for rendering", ToUtf8(pdf.wstring())), e.code());
        }
        UINT const flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                       &impl->device, nullptr, &impl->context);
        if (FAILED(hr))
        {
            // No hardware device (e.g. a remote session): Windows' software rasterizer.
            Log(std::format("pdf render: hardware Direct3D device failed ({}); WARP", DescribeHr(hr)));
            Check("D3D11CreateDevice (WARP)", D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                                                D3D11_SDK_VERSION, &impl->device, nullptr, &impl->context));
        }
        ComPtr<IDXGIDevice> dxgi;
        Check("IDXGIDevice", impl->device.As(&dxgi));
        Check("PdfCreateRenderer", PdfCreateRenderer(dxgi.Get(), &impl->renderer));
        Check("CoCreateInstance(WICImagingFactory)", CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl->wic)));
    }

    PdfAreaRenderer::~PdfAreaRenderer() = default;

    RenderedArea PdfAreaRenderer::Render(int pageIndex, Rect area, double pixelsPerPoint)
    {
        auto& m = *impl;
        if (pageIndex < 0 || static_cast<unsigned>(pageIndex) >= m.document.PageCount())
        {
            ThrowInvalidData(std::format("pdf render: page {} of {}", pageIndex + 1, m.document.PageCount()));
        }
        auto page = m.document.GetPage(static_cast<unsigned>(pageIndex));
        RenderedArea out;
        out.width = std::max(1, static_cast<int>(std::ceil(area.width * pixelsPerPoint)));
        out.height = std::max(1, static_cast<int>(std::ceil(area.height * pixelsPerPoint)));

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(out.width);
        desc.Height = static_cast<UINT>(out.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;
        Check("CreateTexture2D (target)", m.device->CreateTexture2D(&desc, nullptr, &target));
        ComPtr<ID3D11RenderTargetView> view;
        Check("CreateRenderTargetView", m.device->CreateRenderTargetView(target.Get(), nullptr, &view));
        float const transparent[4] = { 0, 0, 0, 0 };
        m.context->ClearRenderTargetView(view.Get(), transparent);

        // Windows.Data.Pdf measures pages in DIPs (points x 96/72); the destination is in pixels.
        double const dip = 96.0 / 72.0;
        PDF_RENDER_PARAMS params{};
        params.SourceRect = D2D_RECT_F{ static_cast<float>(area.x * dip), static_cast<float>(area.y * dip),
                                         static_cast<float>((area.x + area.width) * dip), static_cast<float>((area.y + area.height) * dip) };
        params.DestinationWidth = static_cast<UINT>(out.width);
        params.DestinationHeight = static_cast<UINT>(out.height);
        params.BackgroundColor = D2D_COLOR_F{ 0, 0, 0, 0 }; // transparent: only what the page draws
        params.IgnoreHighContrast = TRUE;
        ComPtr<IDXGISurface> surface;
        Check("IDXGISurface", target.As(&surface));
        Check("RenderPageToSurface", m.renderer->RenderPageToSurface(page.as<::IUnknown>().get(), surface.Get(), POINT{ 0, 0 }, &params));

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Check("CreateTexture2D (staging)", m.device->CreateTexture2D(&desc, nullptr, &staging));
        m.context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check("Map", m.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        UINT const stride = static_cast<UINT>(out.width) * 4;
        std::vector<BYTE> pixels(static_cast<size_t>(stride) * static_cast<size_t>(out.height));
        for (int y = 0; y < out.height; ++y)
        {
            std::memcpy(pixels.data() + static_cast<size_t>(y) * stride, static_cast<BYTE const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, stride);
        }
        m.context->Unmap(staging.Get(), 0);

        // PNG with straight alpha (the surface is premultiplied).
        ComPtr<IWICBitmap> bitmap;
        Check("CreateBitmapFromMemory", m.wic->CreateBitmapFromMemory(static_cast<UINT>(out.width), static_cast<UINT>(out.height),
                                                                       GUID_WICPixelFormat32bppPBGRA, stride,
                                                                       static_cast<UINT>(pixels.size()), pixels.data(), &bitmap));
        ComPtr<IWICFormatConverter> converter;
        Check("CreateFormatConverter", m.wic->CreateFormatConverter(&converter));
        Check("IWICFormatConverter::Initialize", converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                                                        nullptr, 0.0, WICBitmapPaletteTypeCustom));
        ComPtr<IStream> stream;
        Check("CreateStreamOnHGlobal", CreateStreamOnHGlobal(nullptr, TRUE, &stream));
        ComPtr<IWICBitmapEncoder> encoder;
        Check("CreateEncoder (PNG)", m.wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
        Check("IWICBitmapEncoder::Initialize", encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;
        Check("CreateNewFrame", encoder->CreateNewFrame(&frame, nullptr));
        Check("IWICBitmapFrameEncode::Initialize", frame->Initialize(nullptr));
        Check("WriteSource", frame->WriteSource(converter.Get(), nullptr));
        Check("IWICBitmapFrameEncode::Commit", frame->Commit());
        Check("IWICBitmapEncoder::Commit", encoder->Commit());
        STATSTG stat{};
        Check("IStream::Stat", stream->Stat(&stat, STATFLAG_NONAME));
        out.png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
        LARGE_INTEGER zero{};
        Check("IStream::Seek", stream->Seek(zero, STREAM_SEEK_SET, nullptr));
        ULONG read = 0;
        Check("IStream::Read", stream->Read(out.png.data(), static_cast<ULONG>(out.png.size()), &read));
        return out;
    }
}
