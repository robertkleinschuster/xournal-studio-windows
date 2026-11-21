#include "xournal/search/Search.h"

#include "TextMatch.h"
#include "Recognition.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/io/Files.h"
#include "xournal/model/Background.h"
#include "xournal/model/Snapshot.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"
#include "xournal/pdf/TextBoxLayout.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::search
{
    namespace
    {
        using detail::ContextOf;
        using detail::FindMatches;
        using detail::Match;

        // COM/WinRT on the calling thread (an apartment it already has is used as it is).
        class ComScope
        {
        public:
            ComScope()
            {
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (hr == RPC_E_CHANGED_MODE) return;
                Check("CoInitializeEx", hr);
                initialized = true;
            }
            ~ComScope()
            {
                if (initialized) CoUninitialize();
            }
            ComScope(ComScope const&) = delete;
            ComScope& operator=(ComScope const&) = delete;

        private:
            bool initialized = false;
        };

        Rect Union(Rect const& a, Rect const& b)
        {
            if (a.width <= 0 && a.height <= 0) return b;
            double left = std::min(a.x, b.x), top = std::min(a.y, b.y);
            return { left, top, std::max(a.Right(), b.Right()) - left, std::max(a.Bottom(), b.Bottom()) - top };
        }

        // Text with a box and a line per character: the rects of a match are the union of its boxes per line.
        struct Corpus
        {
            std::wstring text;
            std::vector<Rect> boxes;
            std::vector<int> lines;
        };

        std::vector<Rect> RectsOf(Corpus const& corpus, Match const& m)
        {
            std::vector<Rect> rects;
            int line = -1;
            for (size_t i = m.begin; i < m.end && i < corpus.boxes.size(); ++i)
            {
                auto const& box = corpus.boxes[i];
                if (box.width <= 0 && box.height <= 0) continue; // a generated space or line break
                if (corpus.lines[i] != line || rects.empty())
                {
                    rects.push_back(box);
                    line = corpus.lines[i];
                }
                else
                {
                    rects.back() = Union(rects.back(), box);
                }
            }
            return rects;
        }

        void AddHits(std::vector<SearchHit>& hits, HitKind kind, int page, Corpus const& corpus, std::wstring_view query,
                     SearchOptions const& options)
        {
            for (auto const& m : FindMatches(corpus.text, query, options.matchCase, options.wholeWord))
            {
                auto rects = RectsOf(corpus, m);
                if (!rects.empty()) hits.push_back({ kind, page, std::move(rects), ContextOf(corpus.text, m) });
            }
        }

        // The PDF page's text, its boxes scaled from the shown PDF page to the document's page.
        Corpus PdfCorpus(pdftext::TextDocument const& pdf, int pdfPage, double pageWidth, double pageHeight)
        {
            auto shown = pdf.PageSize(pdfPage);
            auto text = pdf.Text(pdfPage);
            double sx = shown.width > 0 ? pageWidth / shown.width : 1, sy = shown.height > 0 ? pageHeight / shown.height : 1;
            Corpus corpus{ std::move(text.text), {}, {} };
            int line = 0;
            for (size_t i = 0; i < corpus.text.size(); ++i)
            {
                auto const& b = text.boxes[i];
                corpus.boxes.push_back({ b.x * sx, b.y * sy, b.width * sx, b.height * sy });
                if (corpus.text[i] == L'\n' || corpus.text[i] == L'\r') ++line;
                corpus.lines.push_back(line);
            }
            return corpus;
        }

        Corpus WordCorpus(std::vector<RecognizedWord> const& words)
        {
            Corpus corpus;
            for (auto const& w : words)
            {
                if (!corpus.text.empty())
                {
                    corpus.text += L' ';
                    corpus.boxes.push_back({});
                    corpus.lines.push_back(w.line);
                }
                for (wchar_t c : w.text)
                {
                    corpus.text += c;
                    corpus.boxes.push_back(w.bounds); // a word's characters share its strokes' bounds
                    corpus.lines.push_back(w.line);
                }
            }
            return corpus;
        }

        Corpus PlacedWords(std::vector<RecognizedWord> words, Size size, Matrix const& transform)
        {
            for (auto& word : words)
            {
                auto r = word.bounds;
                word.bounds = XoppImage::Transform({ r.x * size.width, r.y * size.height,
                    r.width * size.width, r.height * size.height }, transform);
            }
            return WordCorpus(words);
        }

        Rect Transformed(Matrix const& m, double x, double y, double width, double height)
        {
            Point corners[] = { m.Apply({ x, y }), m.Apply({ x + width, y }), m.Apply({ x, y + height }), m.Apply({ x + width, y + height }) };
            double left = corners[0].x, top = corners[0].y, right = left, bottom = top;
            for (auto const& p : corners)
            {
                left = std::min(left, p.x);
                top = std::min(top, p.y);
                right = std::max(right, p.x);
                bottom = std::max(bottom, p.y);
            }
            return { left, top, right - left, bottom - top };
        }

        class TextSearch
        {
        public:
            TextSearch()
            {
                Check("DWriteCreateFactory", DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
            }

            // The page's texts and links: hits measured on the text's own layout (the export's), placed by its transform.
            void AddHits(std::vector<SearchHit>& hits, XmlElement const& page, int pageIndex, std::wstring_view query, SearchOptions const& options)
            {
                for (auto const& layer : page.Elements(L"layer"))
                {
                    for (auto const& e : layer->Elements())
                    {
                        if (!XoppText::IsText(*e)) continue;
                        auto text = XoppText::Read(*e);
                        auto matches = FindMatches(text.content, query, options.matchCase, options.wholeWord);
                        if (matches.empty()) continue;
                        std::wstring family = text.WindowsFamily();
                        auto layout = pdf::TextBoxLayout(factory.Get(), text, family.empty() ? L"Arial" : family, L"de-DE");
                        for (auto const& m : matches)
                        {
                            UINT32 count = 0;
                            auto length = static_cast<UINT32>(m.end - m.begin);
                            HRESULT hr = layout->HitTestTextRange(static_cast<UINT32>(m.begin), length, 0, 0, nullptr, 0, &count);
                            if (hr != E_NOT_SUFFICIENT_BUFFER) Check("HitTestTextRange", hr);
                            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                            Check("HitTestTextRange", layout->HitTestTextRange(static_cast<UINT32>(m.begin), length, 0, 0,
                                                                               metrics.data(), count, &count));
                            SearchHit hit{ HitKind::Text, pageIndex, {}, ContextOf(text.content, m) };
                            for (UINT32 i = 0; i < count; ++i)
                            {
                                auto const& r = metrics[i];
                                if (r.width <= 0) continue;
                                hit.rects.push_back(Transformed(text.transform, r.left, r.top, r.width, r.height));
                            }
                            if (!hit.rects.empty()) hits.push_back(std::move(hit));
                        }
                    }
                }
            }

        private:
            ComPtr<IDWriteFactory> factory;
        };

        std::vector<RecognizedWord> Handwriting(XmlElement const& page, HandwritingCache& cache)
        {
            auto key = StrokesKey(page);
            if (auto cached = cache.Find(key)) return *cached;
            auto words = RecognizeHandwriting(page);
            cache.Store(key, words);
            return words;
        }
    }

    SearchDocument PrepareSearch(XoppDocument& doc, std::shared_ptr<pdftext::TextDocument const> pdf)
    {
        SearchDocument source{ TakeSnapshot(doc, doc.ReferenceBase()), std::move(pdf), doc.PdfPath(), {}, {} };
        for (auto const& page : doc.Pages()) source.backgroundImages.push_back(page->BackgroundImagePath());
        return source;
    }

    bool RecognizeAll(SearchDocument const& doc, HandwritingCache& cache, std::stop_token stop)
    {
        if (!doc.snapshot) ThrowInvalidData("search: no snapshot");
        ComScope com;
        for (auto const& page : doc.snapshot->Elements(L"page"))
        {
            if (stop.stop_requested()) return false;
            Handwriting(*page, cache);
        }
        return true;
    }

    bool Search(SearchDocument const& doc, std::wstring_view query, SearchOptions const& options, HandwritingCache& cache,
                std::function<void(int page, std::vector<SearchHit> hits)> const& onPage, std::stop_token stop)
    {
        if (!doc.snapshot) ThrowInvalidData("search: no snapshot");
        ComScope com;
        TextSearch texts;
        auto visualCache = doc.visualText ? doc.visualText : CreateVisualTextCache();
        detail::VisualText visual(doc, *visualCache);
        int index = 0;
        for (auto const& page : doc.snapshot->Elements(L"page"))
        {
            if (stop.stop_requested()) return false;
            std::vector<SearchHit> hits;
            double width = XoppFormat::ReadNumber(*page, L"width", XoppFormat::A4Width);
            double height = XoppFormat::ReadNumber(*page, L"height", XoppFormat::A4Height);
            auto background = page->Element(L"background");
            if (options.pdf && doc.pdf && background && Background::IsPdf(*background))
            {
                int pdfPage = Background::PdfPageIndex(*background);
                if (pdfPage >= 0 && pdfPage < doc.pdf->PageCount())
                {
                    auto corpus = PdfCorpus(*doc.pdf, pdfPage, width, height);
                    AddHits(hits, HitKind::Pdf, index, corpus, query, options);
                    bool textless = corpus.text.find_first_not_of(L" \t\r\n\f") == std::wstring::npos;
                    auto [words, ink] = visual.PdfPage(pdfPage, options.handwriting, textless, options.images);
                    AddHits(hits, ink ? HitKind::Handwriting : HitKind::PdfOcr, index,
                        PlacedWords(std::move(words), { width, height }, {}), query, options);
                }
            }
            if (options.texts) texts.AddHits(hits, *page, index, query, options);
            if (stop.stop_requested()) return false;
            if (options.handwriting) AddHits(hits, HitKind::Handwriting, index, WordCorpus(Handwriting(*page, cache)), query, options);
            if (options.images)
            {
                if (static_cast<size_t>(index) < doc.backgroundImages.size() && doc.backgroundImages[index])
                {
                    try
                    {
                        auto data = io::Files::ReadAll(*doc.backgroundImages[index]);
                        AddHits(hits, HitKind::Image, index, PlacedWords(visual.Image(data), { width, height }, {}), query, options);
                    }
                    catch (...) { LogException(std::format("search: background image on page {}", index + 1)); }
                }
                for (auto const& layer : page->Elements(L"layer"))
                {
                    for (auto const& e : layer->Elements())
                    {
                        if (stop.stop_requested()) return false;
                        if (!XoppImage::IsImage(*e)) continue;
                        auto data = XoppImage::GetData(*e);
                        if (data.empty() || XoppImage::IsPdf(data)) continue;
                        auto placement = XoppImage::GetPlacement(*e);
                        if (placement.size.width <= 0 || placement.size.height <= 0) continue;
                        AddHits(hits, HitKind::Image, index,
                            PlacedWords(visual.Image(data), placement.size, placement.transform), query, options);
                    }
                }
            }
            if (stop.stop_requested()) return false;
            onPage(index, std::move(hits));
            ++index;
        }
        return true;
    }
}
