// Ported from XournalStudio.Tests/FileFormatTests.cs (documents and pages; references in ReferenceTests.cpp, strokes in
// StrokeTests.cpp, texts in TextTests.cpp).

#include "Samples.h"
#include "TestSupport.h"

#include "xournal/io/DocumentFile.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppPage.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using io::DocumentFile::Load;
    using io::DocumentFile::ReadXml;

    TEST(FileFormat, ADocumentUsesItsFileItsPdfAndTheFoldersHoldingThem)
    {
        TempFolder dir;
        WriteFile(dir / L"lecture.pdf", "", false);
        auto path = WriteFile(dir / L"a.xopp", Sample);
        auto doc = Load(path);

        EXPECT_TRUE(doc->Uses(path));
        EXPECT_TRUE(doc->Uses(dir / L"LECTURE.pdf")); // Windows ignores letter case
        EXPECT_TRUE(doc->Uses(dir.Path()));           // a folder holding it
        EXPECT_FALSE(doc->Uses(dir / L"a.xopp.bak")); // a name that only begins like it
        EXPECT_FALSE(doc->Uses(dir / L"other.pdf"));
    }

    TEST(FileFormat, APageWithoutViewTellsWhetherAnythingLiesOnIt)
    {
        TempFolder dir;
        auto doc = Load(WriteFile(dir / L"h.xopp", Sample));
        EXPECT_TRUE(doc->Pages()[0]->HasContent());
        EXPECT_TRUE(doc->Pages()[1]->HasContent());
        EXPECT_FALSE(doc->Pages()[2]->HasContent());
    }

    TEST(FileFormat, LoadsPagesBackgroundsAndPdfReference)
    {
        TempFolder dir;
        WriteFile(dir / L"lecture.pdf", "", false);
        auto doc = Load(WriteFile(dir / L"a.xopp", Sample));

        ASSERT_EQ(3u, doc->Pages().size());
        EXPECT_TRUE(doc->Pages()[0]->IsPdf());
        EXPECT_EQ(1, doc->Pages()[1]->PdfPageIndex());
        EXPECT_EQ(BackgroundStyle::Graph, doc->Pages()[2]->Style());
        EXPECT_EQ(dir / L"lecture.pdf", *doc->PdfPath());
        EXPECT_EQ(1u, doc->Pages()[0]->Ink().ReadStrokes().size());
        EXPECT_TRUE(doc->Pages()[1]->Ink().ReadStrokes()[0].ink.look.highlighter);
    }

    TEST(FileFormat, UnchangedDocumentIsWrittenBackWithEverythingItContains)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"b.xopp", Sample);
        auto before = ReadXml(path);
        auto doc = Load(path);
        io::DocumentFile::Save(*doc, path);
        auto after = ReadXml(path);

        EXPECT_EQ(L"5", after->AttributeOr(L"fileversion", L""));
        // Nothing is removed, also not attributes of other programs.
        EXPECT_EQ(L"x_kommentiert.pdf", after->AttributeOr(L"fork-export", L""));
        EXPECT_EQ(L"Xournal Studio for Windows", after->AttributeOr(L"creator", L""));
        auto pagesBefore = before->Elements(L"page");
        auto pagesAfter = after->Elements(L"page");
        ASSERT_EQ(pagesBefore.size(), pagesAfter.size());
        for (size_t i = 0; i < pagesBefore.size(); ++i)
        {
            EXPECT_TRUE(Normalized(*pagesBefore[i])->DeepEquals(*Normalized(*pagesAfter[i]))) << "page " << i + 1;
        }
        EXPECT_FALSE(std::filesystem::exists(path.native() + L"~"));
    }

    TEST(FileFormat, SavingKeepsAttributesOfOtherPrograms)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"d.xopp", R"(
<xournal creator="x" fileversion="4" fork-export="e.pdf"><title>t</title>
<page width="100" height="100" fork-page="1"><background type="solid" color="#ffffffff" style="graph"/>
<layer name="L"><teximage text="" left="1" top="2" right="3" bottom="4" portal="abc" portal-area="1 2 3 4">JVBERi0=</teximage>
<stroke tool="pen" color="#000000ff" width="1" capStyle="round" ts="0" fn="" extra="x">1 2 3 4</stroke></layer></page></xournal>)");
        auto doc = Load(path);
        io::DocumentFile::Save(*doc, path);
        auto root = ReadXml(path);
        EXPECT_EQ(L"e.pdf", root->AttributeOr(L"fork-export", L""));
        auto page = root->Element(L"page");
        EXPECT_EQ(L"1", page->AttributeOr(L"fork-page", L""));
        auto layer = page->Element(L"layer");
        EXPECT_EQ(L"L", layer->AttributeOr(L"name", L""));
        auto tex = layer->Element(L"teximage");
        EXPECT_EQ(L"abc", tex->AttributeOr(L"portal", L""));
        EXPECT_EQ(L"JVBERi0=", tex->Value());
        auto stroke = layer->Element(L"stroke");
        EXPECT_EQ(L"x", stroke->AttributeOr(L"extra", L""));
        EXPECT_EQ(L"round", stroke->AttributeOr(L"capStyle", L""));
    }

    TEST(FileFormat, ReadsPlainXmlToo)
    {
        TempFolder dir;
        EXPECT_EQ(3u, Load(WriteFile(dir / L"c.xopp", Sample, false))->Pages().size());
    }

    TEST(FileFormat, NotAXournalDocumentIsRefused)
    {
        TempFolder dir;
        EXPECT_ANY_THROW(Load(WriteFile(dir / L"x.xopp", "<html/>")));
    }

    TEST(FileFormat, FilledAndDashedStrokesAreKeptAndDrawnFromTheFile)
    {
        auto page = XoppPage::FromXml(Xml(R"(
<page width="100" height="100"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#ff0000ff" width="2" fill="128">0 0 50 0 25 40 0 0</stroke>
<stroke tool="pen" color="#000000ff" width="1" style="cust: 2 1.5">0 50 90 50</stroke>
<stroke tool="pen" color="#000000ff" width="1" capStyle="butt">0 60 90 60</stroke>
<stroke tool="pen" color="#000000ff" width="1" capStyle="round">0 70 90 70</stroke>
</layer></page>)"));
        EXPECT_EQ(1u, page->Ink().ReadStrokes().size()); // only the plain one is ink
        ASSERT_EQ(3u, page->DrawnStrokes().size());
        std::wstring custom = L"cust: 2 1.5", dashdot = L"dashdot";
        EXPECT_EQ((std::vector<double>{ 2, 1.5 }), Strokes::Dashes(&custom));
        EXPECT_EQ((std::vector<double>{ 6, 3, 0.5, 3 }), Strokes::Dashes(&dashdot));
        EXPECT_EQ(128, Strokes::ReadShape(*page->DrawnStrokes()[0])->fill);
        int styled = 0;
        for (auto const& e : page->Snapshot()->Element(L"layer")->Elements(L"stroke"))
        {
            styled += e->Attribute(L"fill") || e->Attribute(L"style") || e->AttributeOr(L"capStyle", L"") == L"butt";
        }
        EXPECT_EQ(3, styled);
    }

    TEST(FileFormat, NewDocumentHasUpstreamsHeaderAndAGraphPage)
    {
        auto doc = XoppDocument::CreateNew();
        ASSERT_EQ(1u, doc->Pages().size());
        EXPECT_EQ(BackgroundStyle::Graph, doc->Pages()[0]->Style());
        EXPECT_EQ(L"595.27559", doc->Pages()[0]->Snapshot()->AttributeOr(L"width", L""));
        EXPECT_EQ(L"4", doc->Root().AttributeOr(L"fileversion", L""));
        ASSERT_TRUE(doc->CreatedAt());
        EXPECT_EQ(DocumentNames::DefaultName(*doc->CreatedAt()), doc->SuggestedName());
        EXPECT_EQ(L"2026-11-05-Notiz-09-07", DocumentNames::DefaultName({ 2026, 11, 5, 9, 7 }));
    }
}
