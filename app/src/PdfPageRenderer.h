#pragma once

// PDF pages rendered by Windows.Data.Pdf through its DirectX interop (PdfCreateRenderer /
// IPdfRendererNative::RenderPageToSurface) on a Direct3D 11 device the app owns. PdfPage.RenderToStreamAsync keeps a
// device of its own in a static released only at process exit, where the Intel Iris Xe driver hung (C# app,
// 2026-11-16); this device is released while the app still runs (Shutdown). One page at a time (the interop docs), on
// the render thread only. Pixels come back to memory (BGRA, premultiplied) for the underlay to draw.

#include <d3d11.h>
#include <windows.data.pdf.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Data.Pdf.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace xournal::app
{
    /// Pixels in memory: BGRA, premultiplied, rows of width * 4 bytes.
    struct CpuBitmap
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<std::uint8_t> pixels;
    };

    class PdfPageRenderer
    {
    public:
        /// The page at `width` x `height` pixels, on white (or on nothing when `transparent`). Device loss: retried once
        /// on a new device.
        std::shared_ptr<CpuBitmap> Render(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height, bool transparent);

        /// Releases the device (before the process ends; no rendering afterwards).
        void Shutdown();

    private:
        void EnsureDevice();
        std::shared_ptr<CpuBitmap> RenderOnce(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height, bool transparent);

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IPdfRendererNative> renderer;
        bool shutDown = false;
    };
}
