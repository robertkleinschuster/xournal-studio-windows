#pragma once

// A PDF's page sizes in points, read with Windows.Data.Pdf (for a new document on a PDF). Blocking: call it off the UI
// thread (the thread pool; the process keeps an MTA for it).

#include "xournal/model/Geometry.h"

#include <filesystem>
#include <vector>

namespace xournal::app::PdfPages
{
    std::vector<Size> SizesOf(std::filesystem::path const& pdf);
}
