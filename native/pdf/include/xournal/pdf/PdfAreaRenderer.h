#pragma once

// Renders areas of a PDF's pages as PNG pictures with Windows.Data.Pdf: PdfCreateRenderer +
// IPdfRendererNative::RenderPageToSurface on a Direct3D 11 device this object owns (released with it; the app's
// PdfRenderer route - RenderToStreamAsync keeps a static device whose release hung at exit with the Intel driver).
// A transparent background renders only what the page draws (e.g. its annotations). One page at a time (the docs:
// parallel calls may give unexpected results). Blocking WinRT calls: not on an STA thread.

#include "xournal/model/Geometry.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace xournal::pdf
{
    struct RenderedArea
    {
        std::vector<std::uint8_t> png;
        int width = 0, height = 0; // pixels
    };

    class PdfAreaRenderer
    {
    public:
        /// Loads the PDF (Windows.Data.Pdf) and creates the device. Throws xournal::Error.
        explicit PdfAreaRenderer(std::filesystem::path const& pdf);
        ~PdfAreaRenderer();
        PdfAreaRenderer(PdfAreaRenderer const&) = delete;
        PdfAreaRenderer& operator=(PdfAreaRenderer const&) = delete;

        /// `area` of page `pageIndex` (points, top left, as the page is shown) at `pixelsPerPoint`; transparent where the
        /// page draws nothing.
        RenderedArea Render(int pageIndex, Rect area, double pixelsPerPoint);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
