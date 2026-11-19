#include "xournal/pdftext/PdfText.h"

#include "Pdfium.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/pdf/PageFrame.h"

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <algorithm>
#include <format>

namespace xournal::pdftext
{
    using detail::PdfiumLock;

    bool Available() { return detail::EnsurePdfium(); }

    std::string UnavailableReason()
    {
        detail::EnsurePdfium();
        return detail::PdfiumError();
    }

    namespace
    {
        constexpr int ContextChars = 40;

        // A loaded page and its text page, closed on every exit. Call with the PDFium lock held.
        class OpenPage
        {
        public:
            OpenPage(FPDF_DOCUMENT doc, int index)
            {
                page = FPDF_LoadPage(doc, index);
                if (!page) ThrowInvalidData(std::format("PDF text: page {}: {}", index + 1, detail::LastPdfiumError()));
                text = FPDFText_LoadPage(page);
                if (!text)
                {
                    FPDF_ClosePage(page);
                    ThrowInvalidData(std::format("PDF text: the text of page {} cannot be read", index + 1));
                }
                FS_RECTF box{};
                if (!FPDF_GetPageBoundingBox(page, &box))
                {
                    FPDFText_ClosePage(text);
                    FPDF_ClosePage(page);
                    ThrowInvalidData(std::format("PDF text: page {} has no box", index + 1));
                }
                // FPDF_GetPageBoundingBox = media box intersected with crop box; rotation in quarter turns.
                pdf::PdfBox visible{ box.left, box.bottom, box.right, box.top };
                frame = pdf::PageFrame::Of(visible, visible, static_cast<long long>(FPDFPage_GetRotation(page)) * 90);
            }
            ~OpenPage()
            {
                FPDFText_ClosePage(text);
                FPDF_ClosePage(page);
            }
            OpenPage(OpenPage const&) = delete;
            OpenPage& operator=(OpenPage const&) = delete;

            FPDF_PAGE page = nullptr;
            FPDF_TEXTPAGE text = nullptr;
            pdf::PageFrame frame;
        };

        // The page's text, one UTF-16 unit per entry with the PDFium character it came from.
        struct Units
        {
            std::wstring text;
            std::vector<int> charIndex;
        };

        Units UnitsOf(FPDF_TEXTPAGE text)
        {
            Units units;
            int count = FPDFText_CountChars(text);
            for (int i = 0; i < count; ++i)
            {
                unsigned int u = FPDFText_GetUnicode(text, i);
                if (u >= 0x10000)
                {
                    u -= 0x10000;
                    units.text += static_cast<wchar_t>(0xD800 + (u >> 10));
                    units.text += static_cast<wchar_t>(0xDC00 + (u & 0x3FF));
                    units.charIndex.insert(units.charIndex.end(), { i, i });
                }
                else
                {
                    units.text += static_cast<wchar_t>(u);
                    units.charIndex.push_back(i);
                }
            }
            return units;
        }

        std::wstring Context(Units const& units, int firstChar, int charCount)
        {
            auto begin = std::lower_bound(units.charIndex.begin(), units.charIndex.end(), firstChar) - units.charIndex.begin();
            auto end = std::lower_bound(units.charIndex.begin(), units.charIndex.end(), firstChar + charCount) - units.charIndex.begin();
            auto from = std::max<std::ptrdiff_t>(0, begin - ContextChars);
            auto to = std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(units.text.size()), end + ContextChars);
            std::wstring context;
            for (auto i = from; i < to; ++i)
            {
                wchar_t c = units.text[static_cast<size_t>(i)];
                bool space = c == L'\r' || c == L'\n' || c == L' ' || c == L'\t' || c == 0xFFFE || c == 2;
                if (space)
                {
                    if (!context.empty() && context.back() != L' ') context += L' ';
                }
                else
                {
                    context += c;
                }
            }
            while (!context.empty() && context.back() == L' ') context.pop_back();
            return context;
        }
    }

    struct TextDocument::Impl
    {
        std::vector<std::uint8_t> data; // PDFium reads from it while the document is open
        FPDF_DOCUMENT doc = nullptr;
        int pages = 0;
    };

    TextDocument::TextDocument(std::span<std::uint8_t const> pdf) : impl(std::make_unique<Impl>())
    {
        if (!detail::EnsurePdfium())
        {
            ThrowHr(detail::PdfiumError(), HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
        }
        impl->data.assign(pdf.begin(), pdf.end());
        std::lock_guard lock(PdfiumLock());
        impl->doc = FPDF_LoadMemDocument64(impl->data.data(), impl->data.size(), nullptr);
        if (!impl->doc) ThrowInvalidData("PDF text: " + detail::LastPdfiumError());
        impl->pages = FPDF_GetPageCount(impl->doc);
    }

    TextDocument::~TextDocument()
    {
        if (impl && impl->doc)
        {
            std::lock_guard lock(PdfiumLock());
            FPDF_CloseDocument(impl->doc);
        }
    }

    int TextDocument::PageCount() const { return impl->pages; }

    Size TextDocument::PageSize(int page) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        std::lock_guard lock(PdfiumLock());
        OpenPage open(impl->doc, page);
        return open.frame.Shown();
    }

    PageText TextDocument::Text(int page) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        std::lock_guard lock(PdfiumLock());
        OpenPage open(impl->doc, page);
        auto units = UnitsOf(open.text);
        PageText result{ std::move(units.text), {} };
        result.boxes.reserve(units.charIndex.size());
        for (int i : units.charIndex)
        {
            FS_RECTF box{};
            if (FPDFText_IsGenerated(open.text, i) == 1 || !FPDFText_GetLooseCharBox(open.text, i, &box))
            {
                result.boxes.push_back(Rect{});
                continue;
            }
            result.boxes.push_back(open.frame.Area(pdf::PdfBox{ box.left, box.bottom, box.right, box.top }));
        }
        return result;
    }

    std::vector<Hit> TextDocument::Find(int page, std::wstring_view query, FindOptions options) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        std::vector<Hit> hits;
        if (query.empty()) return hits;
        std::wstring needle(query); // PDFium wants it null-terminated (FPDF_WIDESTRING = UTF-16LE)
        unsigned long flags = (options.matchCase ? FPDF_MATCHCASE : 0) | (options.wholeWord ? FPDF_MATCHWHOLEWORD : 0);
        std::lock_guard lock(PdfiumLock());
        OpenPage open(impl->doc, page);
        Units units;
        bool unitsRead = false;
        FPDF_SCHHANDLE search = FPDFText_FindStart(open.text, reinterpret_cast<FPDF_WIDESTRING>(needle.c_str()), flags, 0);
        if (!search) ThrowInvalidData(std::format("PDF text: search on page {} cannot start", page + 1));
        while (FPDFText_FindNext(search))
        {
            int first = FPDFText_GetSchResultIndex(search);
            int count = FPDFText_GetSchCount(search);
            Hit hit{ page, {}, {} };
            int rects = FPDFText_CountRects(open.text, first, count);
            for (int i = 0; i < rects; ++i)
            {
                double left = 0, top = 0, right = 0, bottom = 0;
                if (FPDFText_GetRect(open.text, i, &left, &top, &right, &bottom))
                {
                    hit.rects.push_back(open.frame.Area(pdf::PdfBox{ left, bottom, right, top }));
                }
            }
            if (!unitsRead)
            {
                units = UnitsOf(open.text);
                unitsRead = true;
            }
            hit.context = Context(units, first, count);
            hits.push_back(std::move(hit));
        }
        FPDFText_FindClose(search);
        return hits;
    }

    bool TextDocument::FindAll(std::wstring_view query, FindOptions options,
                               std::function<void(int page, std::vector<Hit> hits)> const& onPage, std::stop_token stop) const
    {
        for (int page = 0; page < impl->pages; ++page)
        {
            if (stop.stop_requested()) return false;
            onPage(page, Find(page, query, options)); // the lock is held per page only
        }
        return true;
    }
}
