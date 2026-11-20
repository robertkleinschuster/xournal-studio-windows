#pragma once

#include <d3d11.h>
#include <windows.data.pdf.interop.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Pdf.h>

#include <cstdint>
#include <vector>

namespace xournal::pdf
{
    struct NativePdfBitmap
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<std::uint8_t> pixels; // tightly packed, premultiplied BGRA
    };

    // One caller at a time, on the thread that owns the immediate D3D context.
    class NativePdfRenderer
    {
    public:
        NativePdfBitmap Render(winrt::Windows::Data::Pdf::PdfPage const& page, PDF_RENDER_PARAMS const& params);
        void Reset();

    private:
        void EnsureDevice();

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        Microsoft::WRL::ComPtr<IPdfRendererNative> renderer;
    };
}
