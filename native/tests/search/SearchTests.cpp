// The document search: German matching, the PDF's text, the file's texts and handwriting recognised by Windows.
// Synthetic documents only: XML written here, a PDF built with qpdf, handwriting drawn along a font's glyph outlines.

#include "TestSupport.h"

#include "../../search/src/TextMatch.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppDocument.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/search/Search.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <d2d1.h>
#include <dwrite.h>
#include <gtest/gtest.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace xournal::tests
{
    namespace
    {
        using namespace xournal::search;

        // ------------------------------------------------------------------ synthetic handwriting

        // Collects a glyph outline as polylines (Beziers sampled): each figure one stroke, as a pen would draw it.
        class Polylines : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ID2D1SimplifiedGeometrySink>
        {
        public:
            std::vector<std::vector<Point>> figures;
            STDMETHODIMP_(void) SetFillMode(D2D1_FILL_MODE) override {}
            STDMETHODIMP_(void) SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
            STDMETHODIMP_(void) BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) override { figures.push_back({ { p.x, p.y } }); }
            STDMETHODIMP_(void) AddLines(D2D1_POINT_2F const* points, UINT32 count) override
            {
                for (UINT32 i = 0; i < count; ++i) figures.back().push_back({ points[i].x, points[i].y });
            }
            STDMETHODIMP_(void) AddBeziers(D2D1_BEZIER_SEGMENT const* b, UINT32 count) override
            {
                for (UINT32 i = 0; i < count; ++i)
                {
                    Point p0 = figures.back().back();
                    for (int k = 1; k <= 6; ++k)
                    {
                        double t = k / 6.0, u = 1 - t;
                        figures.back().push_back({ u * u * u * p0.x + 3 * u * u * t * b[i].point1.x + 3 * u * t * t * b[i].point2.x + t * t * t * b[i].point3.x,
                                                   u * u * u * p0.y + 3 * u * u * t * b[i].point1.y + 3 * u * t * t * b[i].point2.y + t * t * t * b[i].point3.y });
                    }
                }
            }
            STDMETHODIMP_(void) EndFigure(D2D1_FIGURE_END) override { figures.back().push_back(figures.back().front()); }
            STDMETHODIMP Close() override { return S_OK; }
        };

        // <stroke> elements writing `text` with its baseline at (x, y): the outlines of a thin font's glyphs.
        std::string Handwriting(std::wstring const& text, double x, double y, double size = 36)
        {
            ComPtr<IDWriteFactory> factory;
            Check("DWriteCreateFactory", DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
            ComPtr<IDWriteFontCollection> fonts;
            Check("GetSystemFontCollection", factory->GetSystemFontCollection(&fonts));
            UINT32 index = 0;
            BOOL exists = FALSE;
            Check("FindFamilyName", fonts->FindFamilyName(L"Segoe UI", &index, &exists));
            ComPtr<IDWriteFontFamily> family;
            Check("GetFontFamily", fonts->GetFontFamily(index, &family));
            ComPtr<IDWriteFont> font;
            Check("GetFirstMatchingFont", family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font));
            ComPtr<IDWriteFontFace> face;
            Check("CreateFontFace", font->CreateFontFace(&face));
            std::vector<UINT32> codepoints(text.begin(), text.end());
            std::vector<UINT16> glyphs(codepoints.size());
            Check("GetGlyphIndices", face->GetGlyphIndices(codepoints.data(), static_cast<UINT32>(codepoints.size()), glyphs.data()));
            auto sink = Microsoft::WRL::Make<Polylines>();
            Check("GetGlyphRunOutline", face->GetGlyphRunOutline(static_cast<FLOAT>(size), glyphs.data(), nullptr, nullptr,
                                                                 static_cast<UINT32>(glyphs.size()), FALSE, FALSE, sink.Get()));
            std::string xml;
            for (auto const& figure : sink->figures)
            {
                std::string points;
                for (auto const& p : figure) points += std::format("{:.2f} {:.2f} ", x + p.x, y + p.y);
                xml += std::format("<stroke tool=\"pen\" color=\"#000000ff\" width=\"1.2\">{}</stroke>\n", points);
            }
            return xml;
        }

        // ------------------------------------------------------------------ documents

        std::vector<std::uint8_t> TextPdf()
        {
            QPDF pdf;
            pdf.emptyPDF();
            auto font = QPDFObjectHandle::newDictionary();
            font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
            font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
            font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
            font.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
            auto fonts = QPDFObjectHandle::newDictionary();
            fonts.replaceKey("/F1", pdf.makeIndirectObject(font));
            auto resources = QPDFObjectHandle::newDictionary();
            resources.replaceKey("/Font", fonts);
            auto page = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, 595, 842)));
            page.replaceKey("/Resources", resources);
            page.replaceKey("/Contents", pdf.newStream("BT /F1 12 Tf 100 742 Td (Die Stra\337e der Induktivit\344t) Tj ET\n"));
            pdf.addPage(pdf.makeIndirectObject(page), false);
            QPDFWriter writer(pdf);
            writer.setOutputMemory();
            writer.write();
            std::unique_ptr<Buffer> buffer(writer.getBuffer());
            return { buffer->getBuffer(), buffer->getBuffer() + buffer->getSize() };
        }

        // Page 1: the PDF's page shown at twice its size, a text; page 2: handwriting "Hallo Welt"; page 3: nothing.
        std::unique_ptr<XoppDocument> Document(std::string const& handwriting)
        {
            auto xml = std::format(R"(<xournal fileversion="4">
<page width="1190" height="1684"><background type="pdf" domain="absolute" filename="x.pdf" pageno="1"/><layer>
<text font="Sans" size="20" x="100" y="300" color="#000000ff">Eine Notiz zur Straße
und zur Induktivität</text></layer></page>
<page width="595" height="842"><background type="solid" color="#ffffffff" style="plain"/><layer>{}</layer></page>
<page width="595" height="842"><background type="solid" color="#ffffffff" style="graph"/><layer/></page>
</xournal>)", handwriting);
            return XoppDocument::FromXml(Xml(xml), std::filesystem::temp_directory_path() / L"search-test.xopp");
        }

        std::vector<SearchHit> SearchAll(SearchDocument const& doc, std::wstring_view query, SearchOptions options, HandwritingCache& cache)
        {
            std::vector<SearchHit> all;
            Search(doc, query, options, cache, [&](int, std::vector<SearchHit> hits) { all.insert(all.end(), hits.begin(), hits.end()); });
            return all;
        }
    }

    TEST(SearchMatch, GermanCaseUmlautsAndSharpS)
    {
        using detail::FindMatches;
        EXPECT_EQ(FindMatches(L"Die Straße", L"STRASSE", false, false).size(), 1u); // ß = SS ignoring case
        EXPECT_EQ(FindMatches(L"ÄRGER und ärger", L"ärger", false, false).size(), 2u);
        EXPECT_TRUE(FindMatches(L"ÄRGER", L"ärger", true, false).empty());
        EXPECT_TRUE(FindMatches(L"Arger", L"ärger", false, false).empty()); // a umlaut is another letter
        EXPECT_EQ(FindMatches(L"Wortspiel und Wort.", L"wort", false, true).size(), 1u);
        auto across = FindMatches(L"die erste\r\nzweite Zeile", L"erste zweite", false, false);
        ASSERT_EQ(across.size(), 1u);
        EXPECT_EQ(across[0].begin, 4u);
        EXPECT_EQ(across[0].end, 17u); // through "zweite" in the original
        EXPECT_EQ(detail::ContextOf(L"die erste\r\nzweite Zeile", across[0]), L"die erste zweite Zeile");
    }

    TEST(Search, PdfTextAndFileTexts)
    {
        ASSERT_TRUE(pdftext::Available()) << pdftext::UnavailableReason();
        auto doc = Document("");
        auto pdf = std::make_shared<pdftext::TextDocument const>(TextPdf());
        auto source = PrepareSearch(*doc, pdf);
        HandwritingCache cache;
        auto hits = SearchAll(source, L"STRASSE", { .handwriting = false }, cache);
        ASSERT_EQ(hits.size(), 2u);
        EXPECT_EQ(hits[0].kind, HitKind::Pdf);
        EXPECT_EQ(hits[0].page, 0);
        ASSERT_EQ(hits[0].rects.size(), 1u);
        // The PDF text at x 100 (baseline 742 of 842) shown at twice the size: x 200, y about 2 * (842 - 742 - 12).
        EXPECT_NEAR(hits[0].rects[0].x, 2 * 100 + 2 * 50, 2 * 40); // "Die " before it
        EXPECT_GT(hits[0].rects[0].y, 2 * (842 - 742 - 14));
        EXPECT_LT(hits[0].rects[0].Bottom(), 2 * (842 - 742 + 5));
        EXPECT_EQ(hits[0].context, L"Die Straße der Induktivität");
        EXPECT_EQ(hits[1].kind, HitKind::Text);
        ASSERT_EQ(hits[1].rects.size(), 1u);
        EXPECT_GT(hits[1].rects[0].x, 100);
        EXPECT_NEAR(hits[1].rects[0].y, 300, 2); // first line of the text box
        EXPECT_EQ(hits[1].context, L"Eine Notiz zur Straße und zur Induktivität");

        auto across = SearchAll(source, L"straße und", { .handwriting = false }, cache);
        ASSERT_EQ(across.size(), 1u);
        EXPECT_EQ(across[0].rects.size(), 2u); // two lines of the text
        EXPECT_EQ(SearchAll(source, L"Straße", { .matchCase = true, .handwriting = false }, cache).size(), 2u);
        EXPECT_TRUE(SearchAll(source, L"STRASSE", { .matchCase = true, .handwriting = false }, cache).empty());
    }

    TEST(Search, HandwritingIsRecognisedAndCached)
    {
        auto doc = Document(Handwriting(L"Hallo Welt", 60, 200));
        auto source = PrepareSearch(*doc, nullptr);
        HandwritingCache cache;
        using Clock = std::chrono::steady_clock;
        auto t0 = Clock::now();
        auto words = RecognizeHandwriting(*source.snapshot->Elements(L"page")[1]);
        auto t1 = Clock::now();
        std::wstring recognised;
        for (auto const& w : words) recognised += w.text + L" ";
        std::printf("[ timing ] recognition of a page with \"Hallo Welt\": %.0f ms -> \"%s\"\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count(), ToUtf8(recognised).c_str());
        ASSERT_FALSE(words.empty());

        auto hits = SearchAll(source, L"hallo", { .pdf = false, .texts = false }, cache);
        ASSERT_EQ(hits.size(), 1u) << ToUtf8(recognised);
        EXPECT_EQ(hits[0].kind, HitKind::Handwriting);
        EXPECT_EQ(hits[0].page, 1);
        ASSERT_EQ(hits[0].rects.size(), 1u);
        EXPECT_NEAR(hits[0].rects[0].x, 60, 8);       // the word's strokes start at x 60
        EXPECT_LT(hits[0].rects[0].y, 200);           // above the baseline
        EXPECT_EQ(cache.Size(), 2u);                   // every page once; the two pages without strokes share a key

        // Cached: the same strokes are not recognised again (much faster); other strokes are a new entry.
        auto t2 = Clock::now();
        SearchAll(source, L"welt", { .pdf = false, .texts = false }, cache);
        auto t3 = Clock::now();
        double recognisedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double cached = std::chrono::duration<double, std::milli>(t3 - t2).count();
        EXPECT_LT(cached, recognisedMs / 2 + 5);
        EXPECT_EQ(cache.Size(), 2u);
        auto changed = PrepareSearch(*Document(Handwriting(L"Hallo Welt", 61, 200)), nullptr);
        EXPECT_NE(StrokesKey(*changed.snapshot->Elements(L"page")[1]), StrokesKey(*source.snapshot->Elements(L"page")[1]));
    }

    TEST(Search, Cancellable)
    {
        auto doc = Document("");
        auto source = PrepareSearch(*doc, nullptr);
        HandwritingCache cache;
        std::stop_source stop;
        std::vector<int> pages;
        bool finished = Search(source, L"x", {}, cache, [&](int page, std::vector<SearchHit>) { pages.push_back(page); stop.request_stop(); }, stop.get_token());
        EXPECT_FALSE(finished);
        EXPECT_EQ(pages, (std::vector<int>{ 0 }));
    }

    // Speed of the recognition (measured, printed): a page of six lines of handwriting.
    TEST(Search, HandwritingPageTimes)
    {
        std::string strokes;
        for (int line = 0; line < 6; ++line) strokes += Handwriting(L"Die Induktivität einer Spule", 50, 120 + line * 60, 28);
        auto doc = Document(strokes);
        auto source = PrepareSearch(*doc, nullptr);
        auto page = source.snapshot->Elements(L"page")[1];
        auto t0 = std::chrono::steady_clock::now();
        auto words = RecognizeHandwriting(*page);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::wstring first;
        for (auto const& w : words) if (w.line == 0) first += w.text + L" ";
        std::printf("[ timing ] recognition of a page with 6 lines (%zu strokes): %.0f ms, %zu words; line 1: \"%s\"\n",
                    page->Element(L"layer")->Elements(L"stroke").size(), ms, words.size(), ToUtf8(first).c_str());
        EXPECT_GE(words.size(), 6u);
    }
}
