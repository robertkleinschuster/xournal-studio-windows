#include "xournal/pdf/NativePdfRenderer.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <dxgi.h>

#include <cstring>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf
{
    void NativePdfRenderer::EnsureDevice()
    {
        if (renderer) return;
        ComPtr<ID3D11Device> nextDevice;
        ComPtr<ID3D11DeviceContext> nextContext;
        constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, &nextDevice, nullptr, &nextContext);
        if (FAILED(hr))
        {
            Log(std::format("pdf render: hardware Direct3D device failed ({}); WARP", DescribeHr(hr)));
            Check("D3D11CreateDevice (PDF WARP)", D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                nullptr, 0, D3D11_SDK_VERSION, &nextDevice, nullptr, &nextContext));
        }
        ComPtr<IDXGIDevice> dxgi;
        Check("ID3D11Device as IDXGIDevice (PDF)", nextDevice.As(&dxgi));
        ComPtr<IPdfRendererNative> nextRenderer;
        Check("PdfCreateRenderer", PdfCreateRenderer(dxgi.Get(), &nextRenderer));
        device = std::move(nextDevice);
        context = std::move(nextContext);
        renderer = std::move(nextRenderer);
    }

    void NativePdfRenderer::Reset()
    {
        renderer.Reset();
        context.Reset();
        device.Reset();
    }

    NativePdfBitmap NativePdfRenderer::Render(winrt::Windows::Data::Pdf::PdfPage const& page, PDF_RENDER_PARAMS const& params)
    {
        EnsureDevice();
        NativePdfBitmap bitmap{ params.DestinationWidth, params.DestinationHeight };
        size_t stride = static_cast<size_t>(bitmap.width) * 4;
        bitmap.pixels.resize(stride * bitmap.height);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = bitmap.width;
        desc.Height = bitmap.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> target;
        Check("CreateTexture2D (PDF target)", device->CreateTexture2D(&desc, nullptr, &target));
        if (params.BackgroundColor.a < 1.f)
        {
            ComPtr<ID3D11RenderTargetView> view;
            Check("CreateRenderTargetView (PDF)", device->CreateRenderTargetView(target.Get(), nullptr, &view));
            constexpr float transparent[4]{};
            context->ClearRenderTargetView(view.Get(), transparent);
        }

        ComPtr<IDXGISurface> surface;
        Check("ID3D11Texture2D as IDXGISurface", target.As(&surface));
        auto pageUnknown = page.as<IUnknown>();
        auto options = params;
        Check("IPdfRendererNative::RenderPageToSurface",
            renderer->RenderPageToSurface(pageUnknown.get(), surface.Get(), { 0, 0 }, &options));

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Check("CreateTexture2D (PDF staging)", device->CreateTexture2D(&desc, nullptr, &staging));
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check("ID3D11DeviceContext::Map (PDF staging)", context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        for (UINT y = 0; y < bitmap.height; ++y)
            std::memcpy(bitmap.pixels.data() + y * stride,
                static_cast<std::uint8_t const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, stride);
        context->Unmap(staging.Get(), 0);
        return bitmap;
    }
}
