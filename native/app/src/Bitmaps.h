#pragma once

// Bitmaps for the underlay: pictures decoded by WIC into memory, PDFs loaded from memory (the file stays free to rename
// or delete, as with the C# app), and memory bitmaps uploaded to the Direct2D device that draws them.

#include "PdfPageRenderer.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <winrt/Windows.Data.Pdf.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace xournal::app
{
    namespace Bitmaps
    {
        /// A PNG/JPEG/... decoded by WIC (premultiplied BGRA). Throws when it is not a picture WIC can read.
        std::shared_ptr<CpuBitmap> Decode(std::span<std::uint8_t const> data);
        std::shared_ptr<CpuBitmap> DecodeFile(std::filesystem::path const& file);

        /// A PDF read into memory and opened by Windows.Data.Pdf (blocking: not on the UI thread).
        winrt::Windows::Data::Pdf::PdfDocument LoadPdf(std::span<std::uint8_t const> data);
    }

    /// A memory bitmap and its copy on the drawing device (made on first use; again when the device changes).
    class DrawableBitmap
    {
    public:
        explicit DrawableBitmap(std::shared_ptr<CpuBitmap> pixels) : pixels(std::move(pixels)) {}

        UINT Width() const { return pixels->width; }
        UINT Height() const { return pixels->height; }
        ID2D1Bitmap* On(ID2D1DeviceContext* dc);

    private:
        std::shared_ptr<CpuBitmap> pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> uploaded;
        Microsoft::WRL::ComPtr<ID2D1Device> device;
    };
}
