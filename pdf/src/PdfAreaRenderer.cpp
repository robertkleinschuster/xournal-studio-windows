#include "xournal/pdf/PdfAreaRenderer.h"
#include "xournal/pdf/NativePdfRenderer.h"
#include "xournal/pdf/PngEncoding.h"

#include "xournal/base/Error.h"
#include "xournal/io/Files.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cmath>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf
{
    struct PdfAreaRenderer::Impl
    {
        winrt::Windows::Data::Pdf::PdfDocument document{ nullptr };
        NativePdfRenderer renderer;
        ComPtr<IWICImagingFactory> wic;
        bool comInitialized = false;

        ~Impl()
        {
            document = nullptr;
            renderer.Reset();
            wic.Reset();
            if (comInitialized) CoUninitialize();
        }
    };

    PdfAreaRenderer::PdfAreaRenderer(std::filesystem::path const& pdf)
        : PdfAreaRenderer(std::span<std::uint8_t const>{ io::Files::ReadAll(pdf) })
    {}

    PdfAreaRenderer::PdfAreaRenderer(std::span<std::uint8_t const> pdf) : impl(std::make_unique<Impl>())
    {
        Open([&](Impl& m) {
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            winrt::Windows::Storage::Streams::DataWriter writer(stream);
            writer.WriteBytes({ pdf.data(), pdf.data() + pdf.size() });
            writer.StoreAsync().get();
            writer.DetachStream();
            stream.Seek(0);
            m.document = winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(stream).get();
        }, std::format("a PDF of {} bytes in memory", pdf.size()));
    }

    void PdfAreaRenderer::Open(std::function<void(Impl&)> const& load, std::string const& what)
    {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (com != RPC_E_CHANGED_MODE)
        {
            Check("CoInitializeEx", com);
            impl->comInitialized = true;
        }
        try
        {
            load(*impl);
        }
        catch (winrt::hresult_error const& e)
        {
            ThrowHr(std::format("load {} for rendering", what), e.code());
        }
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

        // Windows.Data.Pdf measures pages in DIPs (points x 96/72); the destination is in pixels.
        double const dip = 96.0 / 72.0;
        PDF_RENDER_PARAMS params{};
        params.SourceRect = D2D_RECT_F{ static_cast<float>(area.x * dip), static_cast<float>(area.y * dip),
                                         static_cast<float>((area.x + area.width) * dip), static_cast<float>((area.y + area.height) * dip) };
        params.DestinationWidth = static_cast<UINT>(out.width);
        params.DestinationHeight = static_cast<UINT>(out.height);
        params.BackgroundColor = D2D_COLOR_F{ 0, 0, 0, 0 }; // transparent: only what the page draws
        params.IgnoreHighContrast = TRUE;
        auto bitmap = m.renderer.Render(page, params);
        out.png = EncodePng(*m.wic.Get(), bitmap.width, bitmap.height, bitmap.pixels);
        return out;
    }
}
