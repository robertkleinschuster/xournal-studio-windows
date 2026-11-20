#pragma once

// The text of PDF pages with character boxes, and search with hit rectangles - PDFium (pdfium.dll, delay-loaded on
// first use; see CMakeLists.txt). Places are in shown coordinates as pdf::PdfNavigation's (pdf/PageFrame.h): points,
// origin at the shown page's top left, y down - the crop box turned by /Rotate, as Windows.Data.Pdf renders the page.
//
// Threads: PDFium is not thread-safe, so every call into it is serialized process-wide; any thread may use a
// TextDocument (off the UI thread for whole-document work), and calls on it never run at the same time.

#include "xournal/model/Geometry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::pdftext
{
    /// Loads and initialises PDFium on the first call. False when pdfium.dll is missing or broken: the reason is
    /// logged once (and kept in UnavailableReason); search is then disabled, nothing crashes.
    bool Available();
    std::string UnavailableReason();

    /// A page's text and one box per UTF-16 unit of it (both units of a surrogate pair share one box). Characters
    /// PDFium inserts itself (spaces between words, line breaks) have an empty box (width and height 0).
    struct PageText
    {
        std::wstring text;
        std::vector<Rect> boxes;
    };

    struct FindOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
    };

    struct Hit
    {
        int page = 0;
        std::vector<Rect> rects;   // one per line the hit covers
        std::wstring context;      // the hit with some text around it, line breaks as spaces
    };

    /// A range of a page's characters (PDFium's character indices).
    struct CharRange
    {
        int first = 0;
        int count = 0;
        bool operator==(CharRange const&) const = default;
    };

    /// One page's text kept open for selecting it with the mouse: PDFium's character at a point, the rectangles and the
    /// text of a range (PDFium's own: FPDFText_GetCharIndexAtPos, FPDFText_CountRects/GetRect, FPDFText_GetText), the
    /// word and line around a character. Shown coordinates. Made by TextDocument::OpenPage; it keeps the document open.
    class TextPage
    {
    public:
        ~TextPage();
        TextPage(TextPage const&) = delete;
        TextPage& operator=(TextPage const&) = delete;

        int CharCount() const;
        /// The character at a point or within `tolerance` points of it; nothing where there is no text.
        std::optional<int> CharAt(Point shown, double tolerance) const;
        /// The rectangles of a range, one per run of a line (as PDFium merges them).
        std::vector<Rect> Rects(CharRange range) const;
        /// The text of a range as PDFium extracts it (line breaks as CR LF).
        std::wstring Text(CharRange range) const;
        /// The word around a character: the characters up to the next space or line break on either side (the
        /// character itself when it is a space).
        CharRange WordAt(int index) const;
        /// The line around a character: up to the line breaks PDFium puts between lines.
        CharRange LineAt(int index) const;

        struct Impl;
        explicit TextPage(std::unique_ptr<Impl> impl);

    private:
        std::unique_ptr<Impl> impl;
    };

    /// The range from one character to another, both included, in either order.
    CharRange RangeBetween(int a, int b);

    class TextDocument
    {
    public:
        /// Opens the PDF from memory (the bytes are copied). Throws xournal::Error when PDFium is not available or
        /// cannot read the PDF.
        explicit TextDocument(std::span<std::uint8_t const> pdf);
        ~TextDocument();
        TextDocument(TextDocument const&) = delete;
        TextDocument& operator=(TextDocument const&) = delete;

        int PageCount() const;
        /// The shown page's size (points).
        Size PageSize(int page) const;

        /// The page's text with character boxes; empty for a page without text (e.g. scanned).
        PageText Text(int page) const;

        /// The hits of `query` on one page, in reading order.
        std::vector<Hit> Find(int page, std::wstring_view query, FindOptions options = {}) const;

        /// All pages in order: `onPage(page, hits)` after each page (also with no hits, for progress). Returns false
        /// when `stop` was requested (checked between pages).
        bool FindAll(std::wstring_view query, FindOptions options,
                     std::function<void(int page, std::vector<Hit> hits)> const& onPage, std::stop_token stop = {}) const;

        /// A page kept open for selecting its text (its calls are serialized with every PDFium call). Throws
        /// xournal::Error when the page cannot be read.
        std::unique_ptr<TextPage> OpenPage(int page) const;

    private:
        struct Impl;
        std::shared_ptr<Impl> impl; // shared with the pages kept open
    };
}
