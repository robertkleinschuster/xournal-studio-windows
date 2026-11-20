// PDF text and search with PDFium, on synthetic PDFs built with qpdf (Helvetica, WinAnsi: umlauts as single bytes).

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PageFrame.h"
#include "xournal/pdftext/PdfText.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <format>
#include <memory>
#include <stop_token>

namespace xournal::tests
{
    namespace
    {
        using namespace xournal::pdftext;

        struct TextPdf
        {
            QPDF pdf;
            QPDFObjectHandle font;

            TextPdf()
            {
                pdf.emptyPDF();
                auto f = QPDFObjectHandle::newDictionary();
                f.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
                f.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
                f.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
                f.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
                font = pdf.makeIndirectObject(f);
            }

            QPDFObjectHandle AddPage(std::string const& content, int rotate = 0, bool crop = false)
            {
                auto fonts = QPDFObjectHandle::newDictionary();
                fonts.replaceKey("/F1", font);
                auto resources = QPDFObjectHandle::newDictionary();
                resources.replaceKey("/Font", fonts);
                auto page = QPDFObjectHandle::newDictionary();
                page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
                page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, 595, 842)));
                if (crop) page.replaceKey("/CropBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(36, 36, 559, 806)));
                if (rotate) page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotate));
                page.replaceKey("/Resources", resources);
                page.replaceKey("/Contents", pdf.newStream(content));
                page = pdf.makeIndirectObject(page);
                pdf.addPage(page, false);
                return page;
            }

            std::vector<std::uint8_t> Bytes()
            {
                QPDFWriter writer(pdf);
                writer.setOutputMemory();
                writer.write();
                std::unique_ptr<Buffer> buffer(writer.getBuffer());
                return { buffer->getBuffer(), buffer->getBuffer() + buffer->getSize() };
            }
        };

        // Three pages: umlauts, a line break and words; a turned, cropped page; a page without text (a scan).
        std::vector<std::uint8_t> SamplePdf()
        {
            TextPdf t;
            t.AddPage("BT /F1 14 Tf 72 750 Td (Gr\374\337e aus \326sterreich) Tj ET\n"
                      "BT /F1 12 Tf 72 700 Td (Das ist die erste) Tj 0 -14 Td (zweite Zeile) Tj ET\n"
                      "BT /F1 12 Tf 72 650 Td (Wortspiel und Wort) Tj ET\n");
            t.AddPage("BT /F1 20 Tf 100 400 Td (Gedreht) Tj ET\n", 90, true);
            t.AddPage("0.5 g 72 72 451 698 re f\n"); // a grey "scan": no text
            return t.Bytes();
        }

        bool Contains(std::wstring const& text, std::wstring const& part) { return text.find(part) != std::wstring::npos; }
    }

    TEST(PdfText, PdfiumIsAvailable)
    {
        EXPECT_TRUE(Available()) << UnavailableReason();
    }

    TEST(PdfText, TextWithUmlautsAndBoxes)
    {
        TextDocument doc(SamplePdf());
        ASSERT_EQ(doc.PageCount(), 3);
        auto text = doc.Text(0);
        ASSERT_EQ(text.text.size(), text.boxes.size());
        EXPECT_TRUE(Contains(text.text, L"Grüße aus Österreich")) << ToUtf8(text.text);
        // The G of "Grüße" (14 pt at x 72, baseline 750): its box starts at x 72, below the page's top by ~92 - ~78.
        auto g = text.text.find(L'G');
        ASSERT_NE(g, std::wstring::npos);
        EXPECT_NEAR(text.boxes[g].x, 72, 1);
        EXPECT_GT(text.boxes[g].y, 842 - 750 - 16);
        EXPECT_LT(text.boxes[g].Bottom(), 842 - 750 + 5);
        EXPECT_EQ(doc.PageSize(0), (Size{ 595, 842 }));
    }

    TEST(PdfText, FindWithCaseAndWholeWord)
    {
        TextDocument doc(SamplePdf());
        auto hits = doc.Find(0, L"grüße");
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].page, 0);
        ASSERT_EQ(hits[0].rects.size(), 1u);
        EXPECT_NEAR(hits[0].rects[0].x, 72, 1);
        EXPECT_TRUE(Contains(hits[0].context, L"Grüße aus Österreich"));
        EXPECT_TRUE(doc.Find(0, L"grüße", { .matchCase = true }).empty());
        EXPECT_EQ(doc.Find(0, L"Wort").size(), 2u);                         // in "Wortspiel" too
        EXPECT_EQ(doc.Find(0, L"Wort", { .wholeWord = true }).size(), 1u);
        EXPECT_TRUE(doc.Find(0, L"").empty());
    }

    TEST(PdfText, HitAcrossALineBreak)
    {
        TextDocument doc(SamplePdf());
        auto hits = doc.Find(0, L"erste zweite");
        ASSERT_EQ(hits.size(), 1u);
        ASSERT_EQ(hits[0].rects.size(), 2u); // one per line
        EXPECT_LT(hits[0].rects[0].y, hits[0].rects[1].y);
        EXPECT_TRUE(Contains(hits[0].context, L"die erste zweite Zeile")) << ToUtf8(hits[0].context);
    }

    TEST(PdfText, RotatedCroppedPageInShownCoordinates)
    {
        TextDocument doc(SamplePdf());
        EXPECT_EQ(doc.PageSize(1), (Size{ 770, 523 }));
        auto hits = doc.Find(1, L"Gedreht");
        ASSERT_EQ(hits.size(), 1u);
        ASSERT_EQ(hits[0].rects.size(), 1u);
        // The text runs along x from (100, 400) in user space; turned by 90 degrees with the crop box (36, 36):
        // shown (x, y) = (y - 36, x - 36) - the word now runs down the page, its baseline at shown x 364.
        auto frame = pdf::PageFrame::Of(pdf::PdfBox{ 36, 36, 559, 806 }, pdf::PdfBox{ 0, 0, 595, 842 }, 90);
        auto origin = frame.FromUserSpace(100, 400);
        auto const& r = hits[0].rects[0];
        EXPECT_NEAR(r.y, origin.y, 2);          // starts where the text starts (plus the G's side bearing)
        EXPECT_GT(r.height, 50);                 // "Gedreht" at 20 pt is ~70 pt long
        EXPECT_LT(r.x, origin.x);                // the glyphs lie on the left of the (turned) baseline...
        EXPECT_GT(r.Right(), origin.x - 25);     // ...within the font's height
        auto text = doc.Text(1);
        auto g = text.text.find(L'G');
        ASSERT_NE(g, std::wstring::npos);
        EXPECT_NEAR(text.boxes[g].y, origin.y, 2);
    }

    TEST(PdfText, PageWithoutText)
    {
        TextDocument doc(SamplePdf());
        EXPECT_TRUE(doc.Text(2).text.empty());
        EXPECT_TRUE(doc.Find(2, L"e").empty());
        EXPECT_THROW(doc.Text(3), Error);
    }

    TEST(PdfText, FindAllIsCancellable)
    {
        TextDocument doc(SamplePdf());
        std::vector<int> pages;
        size_t hits = 0;
        EXPECT_TRUE(doc.FindAll(L"e", {}, [&](int page, std::vector<Hit> h) { pages.push_back(page); hits += h.size(); }));
        EXPECT_EQ(pages, (std::vector<int>{ 0, 1, 2 }));
        EXPECT_GT(hits, 5u);
        std::stop_source stop;
        pages.clear();
        EXPECT_FALSE(doc.FindAll(L"e", {}, [&](int page, std::vector<Hit>) { pages.push_back(page); stop.request_stop(); }, stop.get_token()));
        EXPECT_EQ(pages, (std::vector<int>{ 0 }));
    }

    TEST(PdfText, DamagedPdf)
    {
        std::vector<std::uint8_t> junk{ '%', 'P', 'D', 'F', '-', '1', '.', '7', '\n', 'x' };
        EXPECT_THROW(TextDocument{ junk }, Error);
    }

    // Speed (measured, printed): 500 pages of 40 lines of text each.
    TEST(PdfText, LargeDocumentTimes)
    {
        TextPdf t;
        for (int p = 0; p < 500; ++p)
        {
            std::string content = "BT /F1 10 Tf 60 790 Td 12 TL\n";
            for (int line = 0; line < 40; ++line)
            {
                content += std::format("(Seite {} Zeile {}: Die Induktivit\344t einer Spule h\344ngt von ihrer Windungszahl ab.) '\n", p + 1, line + 1);
            }
            content += "ET\n";
            t.AddPage(content);
        }
        auto bytes = t.Bytes();

        using Clock = std::chrono::steady_clock;
        auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        auto t0 = Clock::now();
        TextDocument doc(bytes);
        auto t1 = Clock::now();
        size_t chars = 0;
        for (int p = 0; p < doc.PageCount(); ++p) chars += doc.Text(p).text.size();
        auto t2 = Clock::now();
        size_t hits = 0;
        doc.FindAll(L"induktivität", {}, [&](int, std::vector<Hit> h) { hits += h.size(); });
        auto t3 = Clock::now();
        std::printf("[ timing ] %zu bytes, %d pages: open %.1f ms, Text %.2f ms/page (%zu chars), FindAll %.1f ms (%zu hits, %.2f ms/page)\n",
                    bytes.size(), doc.PageCount(), ms(t0, t1), ms(t1, t2) / doc.PageCount(), chars, ms(t2, t3), hits, ms(t2, t3) / doc.PageCount());
        EXPECT_EQ(hits, 500u * 40u);
    }

    // The mouse's text selection: a character at a point, word, line, rectangles and text of a range.
    TEST(PdfText, PageKeptOpenForSelection)
    {
        TextDocument doc(SamplePdf());
        auto all = doc.Text(0);
        auto page = doc.OpenPage(0);
        EXPECT_EQ(page->CharCount(), static_cast<int>(all.text.size())); // (BMP only: one unit per character)
        auto at = static_cast<int>(all.text.find(L"erste") + 2);         // the s of "erste"
        Rect box = all.boxes[static_cast<size_t>(at)];
        auto hit = page->CharAt({ box.x + box.width / 2, box.y + box.height / 2 }, 1);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(*hit, at);
        EXPECT_FALSE(page->CharAt({ 500, 400 }, 1).has_value()) << "no text there";

        auto word = page->WordAt(at);
        EXPECT_EQ(page->Text(word), L"erste");
        auto line = page->LineAt(at);
        EXPECT_EQ(page->Text(line), L"Das ist die erste");
        auto first = static_cast<int>(all.text.find(L"ist"));
        EXPECT_EQ(page->Text(RangeBetween(at, first)), L"ist die ers") << "either order, both ends included";

        auto rects = page->Rects(word);
        ASSERT_EQ(rects.size(), 1u);
        EXPECT_LE(rects[0].x, box.x + 0.5);
        EXPECT_GE(rects[0].Right(), box.Right() - 0.5);
        auto across = static_cast<int>(all.text.find(L"zweite"));
        EXPECT_EQ(page->Rects(RangeBetween(at, across)).size(), 2u) << "one rectangle per line";
        EXPECT_TRUE(page->Rects({ at, 0 }).empty());
        EXPECT_THROW(page->Text({ at, 1000 }), Error);
    }

    TEST(PdfText, CharAtOnARotatedCroppedPage)
    {
        TextDocument doc(SamplePdf());
        auto all = doc.Text(1);
        auto at = all.text.find(L'd');
        ASSERT_NE(at, std::wstring::npos);
        Rect box = all.boxes[at];
        auto page = doc.OpenPage(1);
        auto hit = page->CharAt({ box.x + box.width / 2, box.y + box.height / 2 }, 1);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(*hit, static_cast<int>(at));
        EXPECT_EQ(page->Text(page->WordAt(*hit)), L"Gedreht");
    }

    TEST(PdfText, AnOpenPageKeepsItsDocument)
    {
        std::unique_ptr<TextPage> page;
        {
            TextDocument doc(SamplePdf());
            page = doc.OpenPage(0);
        }
        EXPECT_GT(page->CharCount(), 0) << "the document closes with its last page";
        EXPECT_THROW(TextDocument(SamplePdf()).OpenPage(3), Error);
    }
}
