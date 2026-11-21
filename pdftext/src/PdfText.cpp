#include "xournal/pdftext/PdfText.h"

#include "Pdfium.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/pdf/PageFrame.h"
#include <fpdf_annot.h>

#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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
        class LoadedPage
        {
        public:
            LoadedPage(FPDF_DOCUMENT doc, int index)
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
            ~LoadedPage()
            {
                FPDFText_ClosePage(text);
                FPDF_ClosePage(page);
            }
            LoadedPage(LoadedPage const&) = delete;
            LoadedPage& operator=(LoadedPage const&) = delete;

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
        ~Impl();
    };

    TextDocument::TextDocument(std::span<std::uint8_t const> pdf) : impl(std::make_shared<Impl>())
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

    TextDocument::~TextDocument() = default; // the document closes with its last page (Impl's destructor)

    TextDocument::Impl::~Impl()
    {
        if (!doc) return;
        std::lock_guard lock(PdfiumLock());
        FPDF_CloseDocument(doc);
    }

    int TextDocument::PageCount() const { return impl->pages; }

    Size TextDocument::PageSize(int page) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        std::lock_guard lock(PdfiumLock());
        LoadedPage open(impl->doc, page);
        return open.frame.Shown();
    }

    std::vector<std::vector<Point>> TextDocument::PenPaths(int page, bool joinSegments) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF paths: page {} of {}", page + 1, impl->pages));
        std::lock_guard lock(PdfiumLock());
        LoadedPage open(impl->doc, page);
        std::vector<std::vector<Point>> paths;
        for (int i = 0, count = FPDFPage_GetAnnotCount(open.page); i < count; ++i)
        {
            std::unique_ptr<std::remove_pointer_t<FPDF_ANNOTATION>, decltype(&FPDFPage_CloseAnnot)> annotation(
                FPDFPage_GetAnnot(open.page, i), FPDFPage_CloseAnnot);
            if (!annotation) ThrowInvalidData("PDF paths: could not load annotation");
            if (FPDFAnnot_GetSubtype(annotation.get()) != FPDF_ANNOT_INK ||
                (FPDFAnnot_GetFlags(annotation.get()) & (FPDF_ANNOT_FLAG_INVISIBLE | FPDF_ANNOT_FLAG_HIDDEN | FPDF_ANNOT_FLAG_NOVIEW)))
                continue;
            for (unsigned long j = 0, pathCount = FPDFAnnot_GetInkListCount(annotation.get()); j < pathCount; ++j)
            {
                auto length = FPDFAnnot_GetInkListPath(annotation.get(), j, nullptr, 0);
                if (!length) continue;
                std::vector<FS_POINTF> points(length);
                if (FPDFAnnot_GetInkListPath(annotation.get(), j, points.data(), length) != length)
                    ThrowInvalidData("PDF paths: could not read ink annotation");
                auto& path = paths.emplace_back();
                for (auto p : points)
                    if (std::isfinite(p.x) && std::isfinite(p.y)) path.push_back(open.frame.FromUserSpace(p.x, p.y));
                if (path.size() < 2) paths.pop_back();
            }
        }
        if (!joinSegments) return paths;
        std::vector<Point> run;
        std::array<unsigned, 4> lastColor{};
        auto finish = [&] {
            // Standalone rules and strokes too short to infer a pen run are not handwriting.
            if (run.size() >= 3) paths.push_back(std::move(run));
            run.clear();
        };
        const int count = FPDFPage_CountObjects(open.page);
        for (int i = 0; i < count; ++i)
        {
            auto object = FPDFPage_GetObject(open.page, i);
            if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_PATH || FPDFPath_CountSegments(object) != 2)
            {
                finish();
                continue;
            }
            int fill = 0;
            FPDF_BOOL stroked = false;
            auto a = FPDFPath_GetPathSegment(object, 0), b = FPDFPath_GetPathSegment(object, 1);
            if (!FPDFPath_GetDrawMode(object, &fill, &stroked) || !stroked || fill != FPDF_FILLMODE_NONE ||
                FPDFPathSegment_GetType(a) != FPDF_SEGMENT_MOVETO || FPDFPathSegment_GetType(b) != FPDF_SEGMENT_LINETO ||
                FPDFPathSegment_GetClose(b))
            {
                finish();
                continue;
            }
            FS_MATRIX m{};
            std::array<unsigned, 4> color{};
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!FPDFPageObj_GetMatrix(object, &m) || !FPDFPathSegment_GetPoint(a, &x0, &y0) ||
                !FPDFPathSegment_GetPoint(b, &x1, &y1) ||
                !FPDFPageObj_GetStrokeColor(object, &color[0], &color[1], &color[2], &color[3]))
                ThrowInvalidData("PDF paths: could not read a stroked line");
            Matrix transform{ m.a, m.b, m.c, m.d, m.e, m.f };
            auto shown = [&](Point local) {
                auto p = transform.Apply(local);
                return open.frame.FromUserSpace(p.x, p.y);
            };
            Point start = shown({ x0, y0 }), end = shown({ x1, y1 });
            if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) || !std::isfinite(end.y) ||
                start == end || color[3] == 0)
            {
                finish();
                continue;
            }
            if (!run.empty() && (color != lastColor || std::abs(run.back().x - start.x) > 0.01 ||
                std::abs(run.back().y - start.y) > 0.01))
                finish();
            if (run.empty()) run.push_back(start);
            run.push_back(end);
            lastColor = color;
        }
        finish();
        return paths;
    }

    PageText TextDocument::Text(int page) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        std::lock_guard lock(PdfiumLock());
        LoadedPage open(impl->doc, page);
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
        LoadedPage open(impl->doc, page);
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

    // ---- a page kept open for the mouse's text selection

    struct TextPage::Impl
    {
        std::shared_ptr<void const> document;   // the document (TextDocument::Impl), open while the page is
        std::unique_ptr<LoadedPage> page;                    // closed under the lock (TextPage's destructor)
    };

    namespace
    {
        bool IsBreak(unsigned int c) { return c == L'\r' || c == L'\n'; }
        bool IsSpace(unsigned int c) { return IsBreak(c) || c == L' ' || c == L'\t' || c == 0xA0 || c == 0xFFFE || c == 2 || c == 0; }

        void CheckRange(int count, CharRange range)
        {
            if (range.first < 0 || range.count < 0 || range.first + range.count > count)
            {
                ThrowInvalidData(std::format("PDF text: characters {}+{} of {}", range.first, range.count, count));
            }
        }
    }

    CharRange RangeBetween(int a, int b) { return { std::min(a, b), std::abs(a - b) + 1 }; }

    std::unique_ptr<TextPage> TextDocument::OpenPage(int page) const
    {
        if (page < 0 || page >= impl->pages) ThrowInvalidData(std::format("PDF text: page {} of {}", page + 1, impl->pages));
        auto open = std::make_unique<TextPage::Impl>();
        open->document = impl;
        std::lock_guard lock(PdfiumLock());
        open->page = std::make_unique<LoadedPage>(impl->doc, page);
        return std::make_unique<TextPage>(std::move(open));
    }

    TextPage::TextPage(std::unique_ptr<Impl> opened) : impl(std::move(opened)) {}

    TextPage::~TextPage()
    {
        std::lock_guard lock(PdfiumLock());
        impl->page.reset(); // before the document may close (it locks again)
    }

    int TextPage::CharCount() const
    {
        std::lock_guard lock(PdfiumLock());
        return FPDFText_CountChars(impl->page->text);
    }

    std::optional<int> TextPage::CharAt(Point shown, double tolerance) const
    {
        std::lock_guard lock(PdfiumLock());
        Point at = impl->page->frame.ToUserSpace(shown);
        int index = FPDFText_GetCharIndexAtPos(impl->page->text, at.x, at.y, tolerance, tolerance);
        if (index == -3) ThrowInvalidData("PDF text: the character at a point cannot be found");
        return index >= 0 ? std::optional(index) : std::nullopt;
    }

    std::vector<Rect> TextPage::Rects(CharRange range) const
    {
        std::lock_guard lock(PdfiumLock());
        CheckRange(FPDFText_CountChars(impl->page->text), range);
        std::vector<Rect> rects;
        if (range.count == 0) return rects;
        int count = FPDFText_CountRects(impl->page->text, range.first, range.count);
        for (int i = 0; i < count; ++i)
        {
            double left = 0, top = 0, right = 0, bottom = 0;
            if (!FPDFText_GetRect(impl->page->text, i, &left, &top, &right, &bottom))
            {
                ThrowInvalidData(std::format("PDF text: rectangle {} of characters {}+{}", i, range.first, range.count));
            }
            rects.push_back(impl->page->frame.Area(pdf::PdfBox{ left, bottom, right, top }));
        }
        return rects;
    }

    std::wstring TextPage::Text(CharRange range) const
    {
        std::lock_guard lock(PdfiumLock());
        CheckRange(FPDFText_CountChars(impl->page->text), range);
        if (range.count == 0) return {};
        // [doc fpdf_text.h] the buffer holds count characters plus the terminator; characters beyond the BMP take two
        // UTF-16 units, so room for twice as many.
        std::vector<unsigned short> buffer(static_cast<size_t>(range.count) * 2 + 1);
        int written = FPDFText_GetText(impl->page->text, range.first, range.count, buffer.data());
        if (written <= 0) return {};
        return std::wstring(reinterpret_cast<wchar_t const*>(buffer.data()), static_cast<size_t>(written - 1));
    }

    CharRange TextPage::WordAt(int index) const
    {
        std::lock_guard lock(PdfiumLock());
        FPDF_TEXTPAGE text = impl->page->text;
        int count = FPDFText_CountChars(text);
        CheckRange(count, { index, 1 });
        if (IsSpace(FPDFText_GetUnicode(text, index))) return { index, 1 };
        int first = index, last = index;
        while (first > 0 && !IsSpace(FPDFText_GetUnicode(text, first - 1))) --first;
        while (last + 1 < count && !IsSpace(FPDFText_GetUnicode(text, last + 1))) ++last;
        return { first, last - first + 1 };
    }

    CharRange TextPage::LineAt(int index) const
    {
        std::lock_guard lock(PdfiumLock());
        FPDF_TEXTPAGE text = impl->page->text;
        int count = FPDFText_CountChars(text);
        CheckRange(count, { index, 1 });
        int first = index, last = index;
        while (first > 0 && !IsBreak(FPDFText_GetUnicode(text, first - 1))) --first;
        while (last + 1 < count && !IsBreak(FPDFText_GetUnicode(text, last + 1))) ++last;
        while (last > first && IsSpace(FPDFText_GetUnicode(text, last))) --last; // a break or space at its end: not part of it
        return { first, last - first + 1 };
    }
}
