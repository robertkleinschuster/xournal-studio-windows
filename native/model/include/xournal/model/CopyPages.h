#pragma once

// Pages of another document to insert into this one (Seiten aus Dokument einfügen).

#include "xournal/model/XoppDocument.h"

#include <functional>
#include <memory>
#include <vector>

namespace xournal
{
    /// Copies of pages of `source` (indices in its order): each page's element with everything on it, also what the app
    /// does not understand. A PDF background refers to page `pdfPageOf(its page in the source's PDF)` of the target's PDF
    /// (the caller appended those pages or the PDF is the same; only the target's first PDF page names the file, as
    /// upstream writes it); a picture background names the source's picture file (written as its path on save).
    std::vector<std::shared_ptr<XoppPage>> CopyPages(XoppDocument& source, std::vector<int> const& indices,
        std::function<int(int)> const& pdfPageOf);
}
