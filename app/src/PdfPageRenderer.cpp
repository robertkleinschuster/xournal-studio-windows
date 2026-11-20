#include "PdfPageRenderer.h"
#include "Bitmaps.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <dxgi.h>

#include <chrono>
#include <format>

namespace xournal::app
{
    std::shared_ptr<CpuBitmap> PdfPageRenderer::Render(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height,
        bool transparent, std::optional<D2D_RECT_F> source)
    {
        if (shutDown)
        {
            ThrowHr("PDF render after shutdown", E_ILLEGAL_METHOD_CALL);
        }
        try
        {
            return RenderOnce(page, width, height, transparent, source);
        }
        catch (Error const& e)
        {
            if (e.Code() != DXGI_ERROR_DEVICE_REMOVED && e.Code() != DXGI_ERROR_DEVICE_RESET)
            {
                throw;
            }
            // The GPU was reset or removed (driver update, sleep): a new device, once.
            Log("pdf render: device lost, recreating");
            renderer.Reset();
            return RenderOnce(page, width, height, transparent, source);
        }
    }

    std::shared_ptr<CpuBitmap> PdfPageRenderer::RenderThumbnail(winrt::Windows::Data::Pdf::PdfPage const& page,
        UINT width, UINT height, bool transparent, std::optional<D2D_RECT_F> source)
    {
        // Render fine strokes before reducing them to the actual thumbnail pixel grid.
        constexpr UINT Supersample = 2;
        if (width <= 8192 && height <= 8192)
        {
            try
            {
                auto result = Bitmaps::Downsample(
                    *Render(page, width * Supersample, height * Supersample, transparent, source), width, height);
                Log(std::format("pdf render: reduced with area filter to {}x{} px", width, height));
                return result;
            }
            catch (...)
            {
                LogException("pdf render: area filter failed, trying direct rendering");
            }
        }
        return Render(page, width, height, transparent, source);
    }

    void PdfPageRenderer::Shutdown()
    {
        shutDown = true;
        renderer.Reset();
    }

    std::shared_ptr<CpuBitmap> PdfPageRenderer::RenderOnce(winrt::Windows::Data::Pdf::PdfPage const& page, UINT width, UINT height,
        bool transparent, std::optional<D2D_RECT_F> source)
    {
        auto start = std::chrono::steady_clock::now();
        PDF_RENDER_PARAMS params{};
        if (source) params.SourceRect = *source;
        params.DestinationWidth = width;
        params.DestinationHeight = height;
        float background = transparent ? 0.0f : 1.0f;
        params.BackgroundColor = { background, background, background, background };
        params.IgnoreHighContrast = TRUE;
        auto bitmap = std::make_shared<CpuBitmap>(renderer.Render(page, params));
        Log(std::format("pdf render: {}x{} px in {} ms", width, height,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
        return bitmap;
    }
}
