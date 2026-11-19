// The pdf module's export (ported from the PdfExport probe's tests): pages written and checked with qpdf; Windows.Data.Pdf
// opens the result. Generated test PDFs only.

#include "PdfTestSupport.h"
#include "TestSupport.h"

#include "../pdf/src/ContentStream.h"
#include "../pdf/src/FontTables.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PdfExport.h"

#include <qpdf/Buffer.hh>

#include <gtest/gtest.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>

namespace xournal::tests
{
    namespace
    {
        using namespace xournal::pdf;

        bool Contains(std::string const& haystack, std::string const& needle) { return haystack.find(needle) != std::string::npos; }

        bool Has(std::vector<std::string> const& list, std::string const& item)
        {
            return std::find(list.begin(), list.end(), item) != list.end();
        }

        QPDFObjectHandle::Rectangle MediaBox(QPDFPageObjectHelper page) { return page.getAttribute("/MediaBox", false).getArrayAsRectangle(); }

        StrokeShape Shape(Color color, double width, std::vector<Point> points, std::vector<double> segmentWidths = {})
        {
            StrokeShape s;
            s.color = color;
            s.width = width;
            s.points = std::move(points);
            s.segmentWidths = std::move(segmentWidths);
            return s;
        }

        XoppText TextAt(std::wstring content, std::wstring family, double x, double y)
        {
            XoppText t;
            t.content = std::move(content);
            t.family = std::move(family);
            t.transform = Matrix::Translation(x, y);
            return t;
        }

        class PdfExportTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                sourceFile = folder / L"Vorlesung Übung.pdf"; // non-ASCII on purpose
                source = WriteSourcePdf(sourceFile);
            }
            TempFolder folder;
            std::filesystem::path sourceFile;
            SourcePdf source;
        };
    }

    TEST(PdfContentStream, NumbersWithThreeDecimalsTransformsWithFive)
    {
        using detail::Number;
        EXPECT_EQ(Number(1.0), "1");
        EXPECT_EQ(Number(-2.25), "-2.25");
        EXPECT_EQ(Number(595.275590551), "595.276");
        EXPECT_EQ(Number(-1e-7), "0");
        detail::ContentStream c;
        c.Transform(1.000049, 0, 0, -1, 0, 841.889763).MoveTo({ 1.23456, 2 });
        EXPECT_EQ(c.Text(), "1.00005 0 0 -1 0 841.88976 cm\n1.235 2 m\n");
    }

    TEST(PdfFontTables, OnlyThePdfTablesStayWithValidChecksums)
    {
        auto data = ReadBytes(L"C:\\Windows\\Fonts\\arial.ttf");
        std::string font(data.begin(), data.end());
        std::string reduced = detail::KeepPdfTables(font);
        auto tags = TableTags(reduced);
        EXPECT_EQ(tags, (std::vector<std::string>{ "OS/2", "cmap", "cvt ", "fpgm", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "prep" }));
        EXPECT_EQ(NumGlyphs(reduced), NumGlyphs(font));
        EXPECT_LT(reduced.size(), font.size());
        // The whole file sums to 0xB1B0AFBA (head.checkSumAdjustment), each table to its directory checksum.
        auto sum = [](std::string_view d) {
            std::uint32_t s = 0;
            for (size_t i = 0; i < d.size(); i += 4)
            {
                std::uint32_t w = 0;
                for (size_t k = 0; k < 4; ++k) w = w << 8 | (i + k < d.size() ? static_cast<unsigned char>(d[i + k]) : 0u);
                s += w;
            }
            return s;
        };
        EXPECT_EQ(sum(reduced), 0xB1B0AFBAu);
        EXPECT_THROW(detail::KeepPdfTables("not a font at all"), Error);
    }

    TEST_F(PdfExportTest, BackgroundPageIsKeptExactlyWithAnnotationsFlattened)
    {
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ sourceFile, 0 }, {} } };
        auto target = folder / L"out ä.pdf";
        auto result = Export(pages, target);
        auto pdf = Open(target);
        ASSERT_EQ(pdf.pages.size(), 1u);
        EXPECT_DOUBLE_EQ(MediaBox(pdf.pages[0]).urx, 595);
        EXPECT_DOUBLE_EQ(MediaBox(pdf.pages[0]).ury, 842);
        EXPECT_FALSE(pdf.pages[0].getObjectHandle().hasKey("/Annots"));
        auto c = Inspect(pdf.pages[0]);
        EXPECT_GE(c.forms, 1);
        EXPECT_TRUE(Contains(c.text, "BACKGROUNDTEXT page one")) << c.text; // still text (Tj with Helvetica)
        EXPECT_TRUE(Contains(c.text, "STAMPTOKEN")) << c.text;                // flattened appearance
        EXPECT_FALSE(Contains(c.text, "HIDDENTOKEN")) << c.text;              // Hidden is left out
        ASSERT_EQ(c.images.size(), 1u);
        EXPECT_EQ(c.images[0].filter, "/DCTDecode");                           // image unchanged
        EXPECT_EQ(c.images[0].rawData, std::string(source.jpeg.begin(), source.jpeg.end()));
        EXPECT_FALSE(result.annotationsLeftOut);
        EXPECT_FALSE(std::filesystem::exists(folder / L"out ä.pdf~"));
    }

    TEST_F(PdfExportTest, MixedPageSizesAndIndexBeyondLastPage)
    {
        std::vector<ExportPage> pages{
            { 420, 298, PdfPageBackground{ sourceFile, 1 }, {} },
            { 595, 842, PdfPageBackground{ sourceFile, 7 }, {} }, // beyond the end: the last page
            { 300, 500, SolidBackground{}, {} },
        };
        auto target = folder / L"out.pdf";
        Export(pages, target);
        auto pdf = Open(target);
        ASSERT_EQ(pdf.pages.size(), 3u);
        EXPECT_DOUBLE_EQ(MediaBox(pdf.pages[0]).urx, 420);
        EXPECT_DOUBLE_EQ(MediaBox(pdf.pages[0]).ury, 298);
        EXPECT_DOUBLE_EQ(MediaBox(pdf.pages[2]).urx, 300);
        EXPECT_TRUE(Contains(Inspect(pdf.pages[0]).text, "SECONDPAGE"));
        EXPECT_TRUE(Contains(Inspect(pdf.pages[1]).text, "SECONDPAGE"));
        EXPECT_EQ(pdf.pages[0].getAttribute("/Resources", false).getKey("/XObject").getKey("/Im0").getObjGen(),
                  pdf.pages[1].getAttribute("/Resources", false).getKey("/XObject").getKey("/Im0").getObjGen()); // copied once
    }

    TEST_F(PdfExportTest, MissingBackgroundsLeaveThePageWhiteAndAreCounted)
    {
        std::vector<ExportPage> pages{
            { 595, 842, PdfPageBackground{ folder / L"gone.pdf", 0 }, {} },
            { 595, 842, PictureBackground{ folder / L"gone.png" }, {} },
            { 595, 842, MissingBackground{}, {} },
        };
        auto result = Export(pages, folder / L"out.pdf");
        EXPECT_EQ(result.missingBackgrounds, 3);
        EXPECT_EQ(Open(folder / L"out.pdf").pages.size(), 3u);
    }

    TEST_F(PdfExportTest, InkIsVectorPathsPerSegment)
    {
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        page.elements.emplace_back(Shape({ 0xFF, 0, 0, 255 }, 2, { { 10, 10 }, { 20, 10 }, { 30, 15 }, { 40, 10 } }, { 0.75, 1.25, 2.5 }));
        auto highlighter = Shape({ 0x7F, 255, 255, 0 }, 12, { { 10, 100 }, { 200, 100 } });
        highlighter.highlighter = true;
        highlighter.cap = StrokeCap::Butt;
        page.elements.emplace_back(highlighter);
        auto filled = Shape({ 0xFF, 255, 0, 0 }, 1, { { 50, 200 }, { 100, 200 }, { 75, 250 } });
        filled.fill = 64;
        page.elements.emplace_back(filled);
        auto dashed = Shape(Colors::Black, 1.5, { { 10, 300 }, { 300.12345, 300 } });
        dashed.dashes = { 6, 3 };
        dashed.cap = StrokeCap::Square;
        page.elements.emplace_back(dashed);
        page.elements.emplace_back(Shape({ 0xFF, 0, 128, 0 }, 5, { { 400, 400 } }));
        std::vector<ExportPage> pages{ page };
        Export(pages, folder / L"out.pdf");
        auto pdf = Open(folder / L"out.pdf");

        auto c = Inspect(pdf.pages[0]);
        auto widths = c.OperandsOf("w");
        for (char const* w : { "0.75 ", "1.25 ", "2.5 ", "12 ", "1.5 " }) EXPECT_TRUE(Has(widths, w)) << w;
        EXPECT_GE(c.operators["S"], 3 + 1 + 1);
        EXPECT_GE(c.operators["f"], 3); // paper, filling, dot
        EXPECT_TRUE(c.images.empty());  // nothing rasterized
        EXPECT_EQ(c.OperandsOf("d"), (std::vector<std::string>{ "[ 6 3 ] 0 " }));
        EXPECT_TRUE(Has(c.OperandsOf("J"), "2 "));
        EXPECT_TRUE(Has(c.OperandsOf("J"), "0 "));
        EXPECT_TRUE(Has(c.OperandsOf("l"), "300.123 300 ")); // 3 decimals
        bool highlighterAlpha = false, fillAlpha = false;
        for (auto& [name, gs] : pdf.pages[0].getAttribute("/Resources", false).getKey("/ExtGState").ditems())
        {
            if (std::abs(gs.getKey("/CA").getNumericValue() - 0x7f / 255.0) < 1e-3) highlighterAlpha = true;
            if (std::abs(gs.getKey("/ca").getNumericValue() - 64 / 255.0) < 1e-3) fillAlpha = true;
        }
        EXPECT_TRUE(highlighterAlpha);
        EXPECT_TRUE(fillAlpha);
    }

    TEST_F(PdfExportTest, PicturesAreFlateWithSoftMaskAndJpegPassesThrough)
    {
        auto alphaPng = MakePng(40, 30, true);
        auto opaquePng = MakePng(40, 30, false);
        auto jpeg = MakeJpeg(32, 32);
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        page.elements.emplace_back(Picture{ alphaPng, { { 80, 60 }, Matrix::Translation(10, 10) } });
        page.elements.emplace_back(Picture{ alphaPng, { { 80, 60 }, Matrix{ 0, 1, -1, 0, 300, 10 } } }); // the same, turned
        page.elements.emplace_back(Picture{ opaquePng, { { 80, 60 }, Matrix::Translation(10, 200) } });
        page.elements.emplace_back(Picture{ jpeg, { { 64, 64 }, Matrix{ 2, 0, 0, 2, 10, 400 } } });
        std::vector<ExportPage> pages{ page };
        Export(pages, folder / L"out.pdf");
        auto pdf = Open(folder / L"out.pdf");
        auto c = Inspect(pdf.pages[0]);
        ASSERT_EQ(c.images.size(), 4u);
        EXPECT_EQ(c.images[0].filter, "/FlateDecode");
        EXPECT_TRUE(c.images[0].softMask);
        EXPECT_EQ(c.images[0].id, c.images[1].id); // one image object for the same data
        EXPECT_EQ(c.images[2].filter, "/FlateDecode");
        EXPECT_FALSE(c.images[2].softMask);
        EXPECT_EQ(c.images[3].filter, "/DCTDecode");
        EXPECT_EQ(c.images[3].rawData, std::string(jpeg.begin(), jpeg.end())); // passed through, not re-encoded
        auto image = pdf.pages[0].getAttribute("/Resources", false).getKey("/XObject").getKey("/Im0");
        EXPECT_TRUE(image.getDict().getKey("/SMask").getDict().getKey("/Filter").isNameAndEquals("/FlateDecode"));
    }

    TEST_F(PdfExportTest, PdfPictureIsAForm)
    {
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        page.elements.emplace_back(Picture{ ReadBytes(sourceFile), { { 200, 283 }, Matrix::Translation(50, 50) } });
        std::vector<ExportPage> pages{ page };
        Export(pages, folder / L"out.pdf");
        auto c = Inspect(Open(folder / L"out.pdf").pages[0]);
        EXPECT_GE(c.forms, 1);
        EXPECT_TRUE(Contains(c.text, "BACKGROUNDTEXT"));
    }

    TEST_F(PdfExportTest, TextIsRealTextWithEmbeddedReducedSubset)
    {
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        auto t = TextAt(L"Lösung: ∫ f(x) dx\nZweite Zeile", L"Segoe UI", 72, 100);
        t.size = 14;
        t.color = { 0xFF, 200, 0, 0 };
        page.elements.emplace_back(t);
        auto wrapped = TextAt(L"Ein langer Absatz, der umbrochen wird, weil er breiter ist als seine Box.", L"Arial", 72, 300);
        wrapped.size = 11;
        wrapped.wrap = 150;
        page.elements.emplace_back(wrapped);
        std::vector<ExportPage> pages{ page };
        auto result = Export(pages, folder / L"out.pdf");
        EXPECT_EQ(result.textRunsAsOutlines, 0);
        KeepForInspection(folder / L"out.pdf", L"texts.pdf");
        auto pdf = Open(folder / L"out.pdf");
        auto c = Inspect(pdf.pages[0]);
        EXPECT_TRUE(Contains(c.text, ToUtf8(L"Lösung: ∫ f(x) dx"))) << c.text;
        EXPECT_TRUE(Contains(c.text, "Zweite Zeile")) << c.text;
        EXPECT_TRUE(Contains(c.text, "umbrochen")) << c.text;
        EXPECT_GE(c.operators["Tm"], 2 + 3); // two lines, the wrapped text at least three

        auto fonts = pdf.pages[0].getAttribute("/Resources", false).getKey("/Font");
        ASSERT_EQ(fonts.getKeys().size(), 2u);
        for (auto& [name, font] : fonts.ditems())
        {
            EXPECT_TRUE(font.getKey("/Subtype").isNameAndEquals("/Type0"));
            auto cid = font.getKey("/DescendantFonts").getArrayItem(0);
            auto file = cid.getKey("/FontDescriptor").getKey("/FontFile2");
            ASSERT_TRUE(file.isStream());
            auto data = file.getStreamData();
            std::string bytes(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
            std::string base = cid.getKey("/BaseFont").getName();
            EXPECT_EQ(base[7], '+') << base; // subset tag
            auto original = ReadBytes(Contains(base, "Segoe") ? L"C:\\Windows\\Fonts\\segoeui.ttf" : L"C:\\Windows\\Fonts\\arial.ttf");
            // Glyph ids kept (CIDToGIDMap /Identity), only the PDF's tables, far smaller than the font.
            EXPECT_EQ(NumGlyphs(bytes), NumGlyphs(std::string(original.begin(), original.end()))) << base;
            EXPECT_FALSE(Has(TableTags(bytes), "GPOS")) << base;
            EXPECT_LT(bytes.size(), original.size() / 4) << base;
        }
    }

    TEST_F(PdfExportTest, FontCollectionIsEmbedded)
    {
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        page.elements.emplace_back(TextAt(L"Cambria aus einer TTC", L"Cambria", 72, 72));
        std::vector<ExportPage> pages{ page };
        auto result = Export(pages, folder / L"out.pdf");
        EXPECT_EQ(result.textRunsAsOutlines, 0);
        EXPECT_TRUE(Contains(Inspect(Open(folder / L"out.pdf").pages[0]).text, "Cambria aus einer TTC"));
    }

    TEST_F(PdfExportTest, SimulatedBoldBecomesOutlines)
    {
        ExportPage page{ 595, 842, SolidBackground{}, {} };
        auto t = TextAt(L"Ink Free fett", L"Ink Free", 72, 72); // Ink Free has no bold face: DirectWrite simulates it
        t.bold = true;
        page.elements.emplace_back(t);
        std::vector<ExportPage> pages{ page };
        auto result = Export(pages, folder / L"out.pdf");
        EXPECT_GT(result.textRunsAsOutlines, 0);
        auto c = Inspect(Open(folder / L"out.pdf").pages[0]);
        EXPECT_EQ(c.operators["Tf"], 0);
        EXPECT_GT(c.operators["c"] + c.operators["l"], 20);
    }

    TEST_F(PdfExportTest, PictureBackgroundIsStretchedAndRulingDrawn)
    {
        auto png = folder / L"paper.png";
        auto data = MakePng(20, 10, false);
        WriteBytes(png, std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
        std::vector<ExportPage> pages{
            { 400, 300, PictureBackground{ png }, {} },
            { 595, 842, SolidBackground{ { 0xFF, 255, 255, 240 }, BackgroundStyle::Graph, std::nullopt }, {} },
            { 595, 842, SolidBackground{ Colors::White, BackgroundStyle::Dotted, L"r1=20" }, {} },
        };
        Export(pages, folder / L"out.pdf");
        auto pdf = Open(folder / L"out.pdf");
        auto picture = Inspect(pdf.pages[0]);
        ASSERT_EQ(picture.images.size(), 1u);
        EXPECT_TRUE(Has(picture.OperandsOf("cm"), "400 0 0 -300 0 300 "));
        auto graph = Inspect(pdf.pages[1]);
        EXPECT_GT(graph.operators["l"], 90);
        EXPECT_EQ(graph.operators["re"], 1); // the paper
        auto dotted = Inspect(pdf.pages[2]);
        EXPECT_GT(dotted.operators["c"], 4 * 1000); // dots: four curves each
    }

    TEST_F(PdfExportTest, NoPersonalMetadata)
    {
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ sourceFile, 0 }, {} } };
        ExportOptions options;
        options.producer = "Xournal Studio tests";
        Export(pages, folder / L"out.pdf", options);
        auto pdf = Open(folder / L"out.pdf");
        auto info = pdf.pdf->getTrailer().getKey("/Info");
        ASSERT_TRUE(info.isDictionary());
        EXPECT_EQ(info.getKeys(), std::set<std::string>{ "/Producer" });
        EXPECT_EQ(info.getKey("/Producer").getUTF8Value(), "Xournal Studio tests");
        EXPECT_FALSE(pdf.pdf->getRoot().hasKey("/Metadata"));
        for (auto& object : pdf.pdf->getAllObjects())
        {
            if (object.isString()) EXPECT_FALSE(Contains(object.getUTF8Value(), "Secret")) << object.unparse();
        }
    }

    TEST_F(PdfExportTest, FailureKeepsTheTargetAndLeavesNoTempFile)
    {
        auto broken = folder / L"broken.pdf";
        WriteBytes(broken, "%PDF-1.7\nthis is not a PDF");
        auto target = folder / L"out.pdf";
        WriteBytes(target, "previous export");
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ broken, 0 }, {} } };
        EXPECT_THROW(Export(pages, target), Error);
        EXPECT_EQ(ReadText(target), "previous export");
        EXPECT_FALSE(std::filesystem::exists(folder / L"out.pdf~"));
    }

    TEST_F(PdfExportTest, RefusesToOverwriteTheBackground)
    {
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ sourceFile, 0 }, {} } };
        auto before = ReadBytes(sourceFile);
        EXPECT_THROW(Export(pages, sourceFile), Error);
        EXPECT_EQ(ReadBytes(sourceFile), before);
    }

    TEST_F(PdfExportTest, WindowsDataPdfOpensTheResult)
    {
        ExportPage notes{ 400, 600, SolidBackground{ Colors::White, BackgroundStyle::Lined, std::nullopt }, {} };
        notes.elements.emplace_back(TextAt(L"Notiz", L"Segoe UI", 20, 20));
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ sourceFile, 0 }, {} }, notes };
        auto target = folder / L"out.pdf";
        Export(pages, target);
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(target.wstring()).get();
        auto pdf = winrt::Windows::Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
        EXPECT_EQ(pdf.PageCount(), 2u);
        EXPECT_NEAR(pdf.GetPage(1).Size().Width, 400 * 96.0 / 72, 0.01); // DIPs
    }

    TEST_F(PdfExportTest, ToMemoryWritesNoFile)
    {
        std::vector<ExportPage> pages{ { 595, 842, PdfPageBackground{ sourceFile, 1 }, {} } };
        ExportResult result;
        auto bytes = ExportToMemory(pages, result);
        EXPECT_EQ(result.pages, 1);
        ASSERT_GT(bytes.size(), 100u);
        EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 5), "%PDF-");
    }
}
