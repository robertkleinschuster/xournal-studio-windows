#pragma once

// Internal: PDFium as a process-wide library - loaded and initialised once, on demand; one lock for every call into
// it (PDFium is not thread-safe, also not across documents).

#include <mutex>
#include <string>

namespace xournal::pdftext::detail
{
    /// Loads pdfium.dll (next to the exe), binds its imports and initialises it, once. Never throws; false with the
    /// reason logged when that failed.
    bool EnsurePdfium();
    std::string const& PdfiumError();

    /// Held around every call into PDFium.
    std::mutex& PdfiumLock();

    /// FPDF_GetLastError as text.
    std::string LastPdfiumError();
}
