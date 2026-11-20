#pragma once

// PDF pages rendered by Windows.Data.Pdf through its DirectX interop (PdfCreateRenderer /
// IPdfRendererNative::RenderPageToSurface) on a Direct3D 11 device the app owns. PdfPage.RenderToStreamAsync keeps a
// device of its own in a static released only at process exit, where the Intel Iris Xe driver hung (C# app,
// 2026-11-16); this device is released while the app still runs (Shutdown). One page at a time (the interop docs), on
// the render thread only. Pixels come back to memory (BGRA, premultiplied) for the underlay to draw.

#include "xournal/pdf/NativePdfRenderer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace xournal::app
{
    /// Pixels in memory: BGRA, premultiplied, rows of width * 4 bytes.
    using CpuBitmap = pdf::NativePdfBitmap;

    class PdfPageRenderer
    {
    public:
        /// The page at `width` x `height` pixels, on white (or on nothing when `transparent`). Device loss: retried once
        /// on a new device. `source`: only that part of the page (DIPs of PdfPage.Size, PDF_RENDER_PARAMS.SourceRect)
        /// scaled to `width` x `height`; nothing = the whole page.
        std::shared_ptr<CpuBitmap> Render(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height, bool transparent,
            std::optional<D2D_RECT_F> source = std::nullopt);
        std::shared_ptr<CpuBitmap> RenderThumbnail(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height,
            bool transparent, std::optional<D2D_RECT_F> source = std::nullopt);

        /// Releases the device (before the process ends; no rendering afterwards).
        void Shutdown();

    private:
        std::shared_ptr<CpuBitmap> RenderOnce(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height, bool transparent,
            std::optional<D2D_RECT_F> source);

        pdf::NativePdfRenderer renderer;
        bool shutDown = false;
    };
}
