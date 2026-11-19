// Exporting a document (the app's one call): the model's document turned into export pages - backgrounds, strokes,
// pictures, texts, skipped elements, missing backgrounds - and never written over its background PDF.

#include "PdfTestSupport.h"
#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppDocument.h"
#include "xournal/model/XoppImage.h"
#include "xournal/pdf/DocumentExport.h"

#include <gtest/gtest.h>
#include <windows.h>
#include <wincrypt.h>

#include <format>

namespace xournal::tests
{
    namespace
    {
        bool Contains(std::string const& haystack, std::string const& needle) { return haystack.find(needle) != std::string::npos; }

        std::string Base64(std::vector<std::uint8_t> const& data)
        {
            DWORD size = 0;
            CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
            std::string text(size, '\0');
            CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &size);
            text.resize(size);
            return text;
        }

        // A document of five pages: the PDF's two pages with notes, a graph page, a picture background, a PDF page of a
        // document whose PDF is gone is tested separately.
        std::string DocumentXml(std::string const& png)
        {
            return std::format(R"(<?xml version="1.0" standalone="no"?>
<xournal creator="test" fileversion="4">
<page width="595" height="842">
<background type="pdf" domain="absolute" filename="lecture.pdf" pageno="1"/>
<layer>
<stroke tool="pen" color="#3333ccff" width="1.41 1.1 1.2">10 20 30 40 50 60</stroke>
<text font="Sans" size="12" x="100" y="100" color="#000000ff">Hallo Welt</text>
<image left="200" top="200" right="280" bottom="260">{}</image>
<unknown-element/>
</layer>
</page>
<page width="420" height="298">
<background type="pdf" pageno="2"/>
<layer><stroke tool="highlighter" color="#ffff007f" width="8.5">1 2 3 4</stroke></layer>
</page>
<page width="595.27559" height="841.88976">
<background type="solid" color="#ffffffff" style="graph"/>
<layer/>
</page>
<page width="300" height="200">
<background type="pixmap" domain="absolute" filename="paper.png"/>
<layer/>
</page>
<page width="300" height="200">
<background type="pixmap" domain="absolute" filename="missing.png"/>
<layer/>
</page>
</xournal>)", png);
        }
    }

    TEST(PdfDocumentExport, ExportsWhatTheDocumentShows)
    {
        TempFolder folder;
        WriteSourcePdf(folder / L"lecture.pdf");
        auto png = MakePng(30, 20, true);
        WriteBytes(folder / L"paper.png", std::string_view(reinterpret_cast<char const*>(png.data()), png.size()));
        auto doc = XoppDocument::FromXml(Xml(DocumentXml(Base64(png))), folder / L"notes.xopp");

        auto source = pdf::PrepareExport(*doc);
        ASSERT_EQ(source.pictureBackgrounds.size(), 5u);
        auto target = folder / L"notes.pdf";
        auto result = pdf::ExportDocument(source, target);

        KeepForInspection(target, L"document.pdf");
        EXPECT_EQ(result.pages, 5);
        EXPECT_EQ(result.skippedElements, 1);    // <unknown-element/>
        EXPECT_EQ(result.missingBackgrounds, 1); // missing.png
        EXPECT_FALSE(result.annotationsLeftOut);
        auto pdf = Open(target);
        ASSERT_EQ(pdf.pages.size(), 5u);
        EXPECT_DOUBLE_EQ(pdf.pages[1].getAttribute("/MediaBox", false).getArrayAsRectangle().urx, 420);

        auto first = Inspect(pdf.pages[0]);
        EXPECT_TRUE(Contains(first.text, "BACKGROUNDTEXT page one")) << first.text;
        EXPECT_TRUE(Contains(first.text, "STAMPTOKEN")) << first.text;
        EXPECT_TRUE(Contains(first.text, "Hallo Welt")) << first.text;
        EXPECT_EQ(first.OperandsOf("w").size(), 3u);         // the pressure stroke: one width per segment
        EXPECT_EQ(first.images.size(), 2u);                  // the PDF's JPEG and the picture
        EXPECT_TRUE(Contains(Inspect(pdf.pages[1]).text, "SECONDPAGE"));
        EXPECT_GT(Inspect(pdf.pages[2]).operators["l"], 90); // graph ruling
        EXPECT_EQ(Inspect(pdf.pages[3]).images.size(), 1u);  // picture background
    }

    TEST(PdfDocumentExport, NeverOverItsBackgroundPdf)
    {
        TempFolder folder;
        WriteSourcePdf(folder / L"lecture.pdf");
        auto doc = XoppDocument::FromXml(Xml(R"(<xournal fileversion="4"><page width="595" height="842">
<background type="solid" color="#ffffffff" style="plain"/><layer/></page>
<page width="595" height="842"><background type="pdf" domain="absolute" filename="lecture.pdf" pageno="1"/><layer/></page></xournal>)"),
                                         folder / L"notes.xopp");
        auto before = ReadBytes(folder / L"lecture.pdf");
        auto source = pdf::PrepareExport(*doc);
        EXPECT_THROW(pdf::ExportDocument(source, folder / L"LECTURE.pdf"), Error); // Windows compares names ignoring case
        EXPECT_EQ(ReadBytes(folder / L"lecture.pdf"), before);
    }

    TEST(PdfDocumentExport, MissingPdfLeavesPagesWhite)
    {
        TempFolder folder;
        auto doc = XoppDocument::FromXml(Xml(R"(<xournal fileversion="4"><page width="595" height="842">
<background type="pdf" domain="absolute" filename="gone.pdf" pageno="1"/><layer>
<stroke tool="pen" color="#000000ff" width="1">1 1 50 50</stroke></layer></page></xournal>)"),
                                         folder / L"notes.xopp");
        auto result = pdf::ExportDocument(pdf::PrepareExport(*doc), folder / L"notes.pdf");
        EXPECT_EQ(result.missingBackgrounds, 1);
        EXPECT_EQ(Inspect(Open(folder / L"notes.pdf").pages[0]).operators["S"], 1);
    }
}
