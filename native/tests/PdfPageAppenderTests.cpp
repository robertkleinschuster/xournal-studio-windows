// Appending pages of another PDF to the document's PDF (port of the C# PdfPageAppender on qpdf).

#include "PdfTestSupport.h"
#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PdfPageAppender.h"

#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <gtest/gtest.h>

namespace xournal::tests
{
    namespace
    {
        bool Contains(std::string const& haystack, std::string const& needle) { return haystack.find(needle) != std::string::npos; }
    }

    TEST(PdfPageAppender, AppendsPagesWithAnnotationsDrawnIntoThem)
    {
        TempFolder folder;
        auto target = folder / L"Vorlesung.pdf";
        WriteSourcePdf(target, { "TARGETTHREE" }); // three pages; its first page keeps its annotations
        auto source = folder / L"iPad Übung.pdf";
        WriteSourcePdf(source, { "SOURCETHREE" });
        auto sourceBefore = ReadBytes(source);

        int first = pdf::AppendPages(target, source, { 0, 2 });

        EXPECT_EQ(first, 3);
        auto pdf = Open(target);
        ASSERT_EQ(pdf.pages.size(), 5u);
        // The pages already there keep their numbers and content (and the first its annotations).
        EXPECT_TRUE(Contains(Inspect(pdf.pages[2]).text, "TARGETTHREE"));
        EXPECT_TRUE(pdf.pages[0].getObjectHandle().hasKey("/Annots"));
        // The appended source page 1: its visible stamp is content now, the hidden one is not drawn, no annotations.
        auto appended = Inspect(pdf.pages[3]);
        EXPECT_TRUE(Contains(appended.text, "BACKGROUNDTEXT page one")) << appended.text;
        EXPECT_TRUE(Contains(appended.text, "STAMPTOKEN")) << appended.text;
        EXPECT_FALSE(Contains(appended.text, "HIDDENTOKEN")) << appended.text;
        EXPECT_FALSE(pdf.pages[3].getObjectHandle().hasKey("/Annots"));
        EXPECT_EQ(appended.images.size(), 1u);
        EXPECT_TRUE(Contains(Inspect(pdf.pages[4]).text, "SOURCETHREE"));
        // The source stays as it is; no temporary file is left.
        EXPECT_EQ(ReadBytes(source), sourceBefore);
        EXPECT_FALSE(std::filesystem::exists(folder / L"Vorlesung.pdf~"));
    }

    TEST(PdfPageAppender, KeepsLinksToTheOutsideDropsInternalOnes)
    {
        TempFolder folder;
        WriteSourcePdf(folder / L"a.pdf");
        auto source = folder / L"b.pdf";
        WriteSourcePdf(source);
        {
            // Page 2 of the source gets a web link and a link to its page 1.
            QPDF pdf;
            std::string path = ToUtf8(source.wstring());
            pdf.processFile(path.c_str());
            auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
            auto link = [&](QPDFObjectHandle action, bool dest) {
                auto a = QPDFObjectHandle::newDictionary();
                a.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
                a.replaceKey("/Subtype", QPDFObjectHandle::newName("/Link"));
                a.replaceKey("/Rect", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(10, 10, 100, 30)));
                a.replaceKey("/P", pages[1].getObjectHandle());
                if (action.isInitialized()) a.replaceKey("/A", action);
                if (dest)
                {
                    auto d = QPDFObjectHandle::newArray();
                    d.appendItem(pages[0].getObjectHandle());
                    d.appendItem(QPDFObjectHandle::newName("/Fit"));
                    a.replaceKey("/Dest", d);
                }
                return pdf.makeIndirectObject(a);
            };
            auto uri = QPDFObjectHandle::newDictionary();
            uri.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
            uri.replaceKey("/URI", QPDFObjectHandle::newString("https://example.org/"));
            auto annots = QPDFObjectHandle::newArray();
            annots.appendItem(link(uri, false));
            annots.appendItem(link(QPDFObjectHandle(), true));
            pages[1].getObjectHandle().replaceKey("/Annots", annots);
            QPDFWriter writer(pdf, (path + ".new").c_str());
            writer.write();
        }
        std::filesystem::rename(folder / L"b.pdf.new", source);

        pdf::AppendPages(folder / L"a.pdf", source, { 1 });

        auto pdf = Open(folder / L"a.pdf");
        ASSERT_EQ(pdf.pages.size(), 3u);
        auto annots = pdf.pages[2].getObjectHandle().getKey("/Annots");
        ASSERT_TRUE(annots.isArray());
        ASSERT_EQ(annots.getArrayNItems(), 1);
        auto kept = annots.getArrayItem(0);
        EXPECT_EQ(kept.getKey("/A").getKey("/URI").getStringValue(), "https://example.org/");
        EXPECT_FALSE(kept.hasKey("/P"));
    }

    TEST(PdfPageAppender, TheSamePageTwice)
    {
        TempFolder folder;
        WriteSourcePdf(folder / L"a.pdf");
        WriteSourcePdf(folder / L"b.pdf");
        EXPECT_EQ(pdf::AppendPages(folder / L"a.pdf", folder / L"b.pdf", { 1, 1 }), 2);
        auto pdf = Open(folder / L"a.pdf");
        ASSERT_EQ(pdf.pages.size(), 4u);
        EXPECT_TRUE(Contains(Inspect(pdf.pages[2]).text, "SECONDPAGE"));
        EXPECT_TRUE(Contains(Inspect(pdf.pages[3]).text, "SECONDPAGE"));
    }

    TEST(PdfPageAppender, AnIndexOutOfRangeChangesNothing)
    {
        TempFolder folder;
        WriteSourcePdf(folder / L"a.pdf");
        WriteSourcePdf(folder / L"b.pdf");
        auto before = ReadBytes(folder / L"a.pdf");
        EXPECT_THROW(pdf::AppendPages(folder / L"a.pdf", folder / L"b.pdf", { 0, 5 }), Error);
        EXPECT_EQ(ReadBytes(folder / L"a.pdf"), before);
        EXPECT_FALSE(std::filesystem::exists(folder / L"a.pdf~"));
    }
}
