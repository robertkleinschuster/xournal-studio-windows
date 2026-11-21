#pragma once

// Searching a document (Strg+F): the background PDF's text (pdftext / PDFium), the file's texts (<text>, <link>) and the
// handwriting (Windows InkAnalyzer) and pictures and scans (Windows OCR). Hits come page by page, cancellable;
// everything runs off the UI thread. No UI here.
//
//   auto doc = search::PrepareSearch(xopp, pdfText);           // where the ink may be read (a snapshot)
//   search::Search(doc, L"Induktivität", {}, cache, onPage, stop); // any thread but an STA (WinRT calls block)
//
// Matching is German, the Windows way (FindNLSStringEx, de-DE): case-insensitive by default (ä = Ä, ß = SS), runs of
// whitespace and line breaks match one space, so a query may run across lines.

#include "xournal/model/Geometry.h"
#include "xournal/model/XoppDocument.h"
#include "xournal/pdftext/PdfText.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::search
{
    enum class HitKind { Pdf, Text, Handwriting, Image, PdfOcr };

    struct SearchHit
    {
        HitKind kind = HitKind::Pdf;
        int page = 0;              // the document's page (0-based)
        std::vector<Rect> rects;   // page coordinates (.xopp units, top left); one per line the hit covers
        std::wstring context;      // the hit with some text around it
    };

    struct SearchOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
        bool pdf = true, texts = true, handwriting = true, images = true;
    };

    /// Recognized text with word bounds in the source's coordinate space and a zero-based line number.
    struct RecognizedWord
    {
        std::wstring text;
        Rect bounds;
        int line = 0;
    };

    /// Recognised handwriting by the strokes it came from (a hash of the page's <stroke> elements): a page whose
    /// strokes did not change is never recognised again. Memory only - never written anywhere. Thread-safe; the app
    /// keeps one while a document is open.
    class HandwritingCache
    {
    public:
        std::optional<std::vector<RecognizedWord>> Find(std::uint64_t strokes) const;
        void Store(std::uint64_t strokes, std::vector<RecognizedWord> words);
        size_t Size() const;
        void Clear();

    private:
        mutable std::mutex lock;
        std::map<std::uint64_t, std::vector<RecognizedWord>> pages;
    };

    class VisualTextCache;
    std::shared_ptr<VisualTextCache> CreateVisualTextCache();

    /// What a search reads: the pages as they are now (a snapshot of the <xournal> element) and, if the document has
    /// a PDF and PDFium is available, its text (the app's pdftext document; may be null).
    struct SearchDocument
    {
        XmlElementPtr snapshot;
        std::shared_ptr<pdftext::TextDocument const> pdf;
        std::optional<std::filesystem::path> pdfPath;
        std::vector<std::optional<std::filesystem::path>> backgroundImages;
        std::shared_ptr<VisualTextCache> visualText;
    };

    /// Takes the snapshot (call it where the document's ink may be read).
    SearchDocument PrepareSearch(XoppDocument& doc, std::shared_ptr<pdftext::TextDocument const> pdf);

    /// The key of a page's handwriting in the cache.
    std::uint64_t StrokesKey(XmlElement const& page);

    /// Recognises one page's handwriting (strokes that are not highlighters) with InkAnalyzer, blocking.
    std::vector<RecognizedWord> RecognizeHandwriting(XmlElement const& page);

    /// Recognises every page not in the cache yet (page by page; for a search ahead of the first Strg+F). Returns
    /// false when stopped.
    bool RecognizeAll(SearchDocument const& doc, HandwritingCache& cache, std::stop_token stop = {});

    /// Searches page by page: `onPage(page, hits)` receives text and recognition hits in page order.
    /// Returns false when stopped between pages or sources. Throws xournal::Error.
    bool Search(SearchDocument const& doc, std::wstring_view query, SearchOptions const& options, HandwritingCache& cache,
                std::function<void(int page, std::vector<SearchHit> hits)> const& onPage, std::stop_token stop = {});
}
