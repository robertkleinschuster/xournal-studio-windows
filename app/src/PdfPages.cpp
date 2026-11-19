#include "PdfPages.h"

#include "xournal/base/Paths.h"

#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

namespace xournal::app::PdfPages
{
    std::vector<Size> SizesOf(std::filesystem::path const& pdf)
    {
        using namespace winrt::Windows;
        // StorageFile rejects forward slashes: the full path has none.
        auto file = Storage::StorageFile::GetFileFromPathAsync(Paths::Full(pdf).native()).get();
        auto document = Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        std::vector<Size> sizes;
        for (uint32_t i = 0; i < document.PageCount(); ++i)
        {
            // PdfPage.Size is in DIPs (points x 96/72).
            auto size = document.GetPage(i).Size();
            sizes.push_back({ size.Width * 72.0 / 96.0, size.Height * 72.0 / 96.0 });
        }
        return sizes;
    }
}
