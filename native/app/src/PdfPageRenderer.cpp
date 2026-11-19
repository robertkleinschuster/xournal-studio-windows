#include "PdfPageRenderer.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <dxgi.h>

#include <cstring>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::app
{
    std::shared_ptr<CpuBitmap> PdfPageRenderer::Render(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height,
        bool transparent)
    {
        if (shutDown)
        {
            ThrowHr("PDF render after shutdown", E_ILLEGAL_METHOD_CALL);
        }
        try
        {
            return RenderOnce(page, width, height, transparent);
        }
        catch (Error const& e)
        {
            if (e.Code() != DXGI_ERROR_DEVICE_REMOVED && e.Code() != DXGI_ERROR_DEVICE_RESET)
            {
                throw;
            }
            // The GPU was reset or removed (driver update, sleep): a new device, once.
            Log("pdf render: device lost, recreating");
            device.Reset();
            context.Reset();
            renderer.Reset();
            return RenderOnce(page, width, height, transparent);
        }
    }

    void PdfPageRenderer::Shutdown()
    {
        shutDown = true;
        renderer.Reset();
        context.Reset();
        device.Reset();
    }

    void PdfPageRenderer::EnsureDevice()
    {
        if (device)
        {
            return;
        }
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        Check("D3D11CreateDevice (PDF)", D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context));
        ComPtr<IDXGIDevice> dxgi;
        Check("ID3D11Device as IDXGIDevice", device.As(&dxgi));
        Check("PdfCreateRenderer", PdfCreateRenderer(dxgi.Get(), &renderer));
    }

    std::shared_ptr<CpuBitmap> PdfPageRenderer::RenderOnce(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height,
        bool transparent)
    {
        EnsureDevice();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> target;
        Check("CreateTexture2D (PDF target)", device->CreateTexture2D(&desc, nullptr, &target));
        ComPtr<IDXGISurface> surface;
        Check("ID3D11Texture2D as IDXGISurface", target.As(&surface));

        PDF_RENDER_PARAMS params{};
        params.DestinationWidth = width;
        params.DestinationHeight = height;
        float background = transparent ? 0.0f : 1.0f;
        params.BackgroundColor = { background, background, background, background };
        params.IgnoreHighContrast = TRUE;
        auto pageUnknown = page.as<IUnknown>();
        Check("IPdfRendererNative::RenderPageToSurface", renderer->RenderPageToSurface(pageUnknown.get(), surface.Get(), { 0, 0 }, &params));

        // Back to memory through a staging copy (the render thread is the only user of this device's context).
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Check("CreateTexture2D (PDF staging)", device->CreateTexture2D(&desc, nullptr, &staging));
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check("ID3D11DeviceContext::Map (PDF staging)", context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        auto bitmap = std::make_shared<CpuBitmap>();
        bitmap->width = width;
        bitmap->height = height;
        bitmap->pixels.resize(static_cast<size_t>(width) * height * 4);
        for (UINT y = 0; y < height; ++y)
        {
            std::memcpy(bitmap->pixels.data() + static_cast<size_t>(y) * width * 4,
                static_cast<std::uint8_t const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(width) * 4);
        }
        context->Unmap(staging.Get(), 0);
        return bitmap;
    }
}
