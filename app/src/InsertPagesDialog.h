#pragma once

// The dialog of Seiten aus PDF oder Dokument einfügen (resource IDD_INSERT_PAGES): the source's pages as thumbnails - a
// tap checks or unchecks a page, "Alle auswählen" checks all - "nach/vor Seite N", and what happens to the document's
// PDF. Thumbnails are asked for page by page while the dialog is open; what arrives after it closed is dropped.

#include "PdfPageRenderer.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xournal::app
{
    struct InsertPagesRequest
    {
        std::wstring file;   // the source's name for the title
        int count = 0;       // its pages
        int around = 0;      // the document's page the new pages go before or after (0-based)
        std::wstring where;  // what happens to the document's PDF
        std::function<double(int index)> aspect;  // a source page's height / width
        /// Renders a source page's thumbnail at `pixels` (the size it is drawn at); `done` runs on the UI thread
        /// (nothing: it could not be rendered).
        std::function<void(int index, SIZE pixels, std::function<void(std::shared_ptr<CpuBitmap>)> done)> render;
    };

    struct InsertPagesChoice
    {
        std::vector<int> pages; // ascending
        bool before = false;
    };

    /// Nothing: cancelled.
    std::optional<InsertPagesChoice> AskInsertPages(HWND owner, InsertPagesRequest const& request);
}
