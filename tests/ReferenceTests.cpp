// Ported from XournalStudio.Tests/FileFormatTests.cs: PDF and picture references, Save As with attachments.

#include "Samples.h"
#include "TestSupport.h"

#include "xournal/base/Paths.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/model/Snapshot.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using namespace io::DocumentFile;

    namespace
    {
        std::wstring PdfFilename(std::filesystem::path const& file)
        {
            return ReadXml(file)->Element(L"page")->Element(L"background")->AttributeOr(L"filename", L"<none>");
        }

        std::string Replaced(std::string text, std::string const& from, std::string const& to)
        {
            text.replace(text.find(from), from.size(), to);
            return text;
        }

        std::vector<XmlElementPtr> Backgrounds(XmlElement const& snapshot)
        {
            std::vector<XmlElementPtr> backgrounds;
            for (auto const& page : snapshot.Elements(L"page"))
            {
                backgrounds.push_back(page->Element(L"background"));
            }
            return backgrounds;
        }
    }

    TEST(References, UnsavedDocumentFollowsItsRenamedPdf)
    {
        TempFolder dir;
        auto doc = XoppDocument::FromPdf(dir / L"alt.pdf", { { 595, 842 } });
        doc->MovePdf(dir / L"neu.pdf");
        auto path = dir / L"neu.xopp";
        Save(*doc, path);
        EXPECT_EQ(dir / L"neu.pdf", *Load(path)->PdfPath());
    }

    TEST(References, LoadedDocumentFollowsItsRenamedPdf)
    {
        TempFolder dir;
        WriteFile(dir / L"lecture.pdf", "", false);
        auto path = WriteFile(dir / L"a.xopp", Sample);
        auto doc = Load(path);
        std::filesystem::rename(dir / L"lecture.pdf", dir / L"Skript.pdf");
        doc->MovePdf(dir / L"Skript.pdf");
        Save(*doc, path);
        EXPECT_EQ(L"Skript.pdf", PdfFilename(path));
    }

    TEST(References, NewDocumentRefersToItsPdfRelatively)
    {
        TempFolder dir;
        std::filesystem::create_directories(dir / L"VO");
        auto doc = XoppDocument::FromPdf(dir / L"VO" / L"Skript 1.pdf", { { 595, 842 } });
        auto path = dir / L"a.xopp";
        Save(*doc, path);
        EXPECT_EQ(L"VO/Skript 1.pdf", PdfFilename(path));
        EXPECT_EQ(dir / L"VO" / L"Skript 1.pdf", *Load(path)->PdfPath());
    }

    TEST(References, LoadedRelativeReferenceIsKeptAndFollowsASavedCopy)
    {
        TempFolder dir;
        WriteFile(dir / L"lecture.pdf", "", false);
        auto path = WriteFile(dir / L"a.xopp", Sample);
        auto doc = Load(path);
        Save(*doc, path);
        EXPECT_EQ(L"lecture.pdf", PdfFilename(path));

        std::filesystem::create_directories(dir / L"Kopie");
        auto copy = dir / L"Kopie" / L"a.xopp";
        Save(*doc, copy);
        EXPECT_EQ(L"../lecture.pdf", PdfFilename(copy));
        EXPECT_EQ(dir / L"lecture.pdf", *Load(copy)->PdfPath());
    }

    TEST(References, LoadedAbsoluteReferenceIsRepairedAsAbsolute)
    {
        // Written on another machine; the PDF is found next to the .xopp.
        TempFolder dir;
        auto pdf = dir / L"lecture.pdf";
        WriteFile(pdf, "", false);
        auto path = WriteFile(dir / L"a.xopp", Replaced(Sample, "filename=\"lecture.pdf\"", "filename=\"Z:/elsewhere/lecture.pdf\""));
        auto doc = Load(path);
        Save(*doc, path);
        EXPECT_EQ(Paths::WithForwardSlashes(pdf), PdfFilename(path));
    }

    TEST(References, AttachedPdfIsReferencedByPathFromAnotherFile)
    {
        // "attach" names "<document>.xopp.bg.pdf": a copy under another name must name the real file instead.
        TempFolder dir;
        auto path = WriteFile(dir / L"a.xopp",
            Replaced(Sample, "domain=\"absolute\" filename=\"lecture.pdf\"", "domain=\"attach\" filename=\"bg.pdf\""));
        WriteFile(path.native() + L".bg.pdf", "", false);
        auto doc = Load(path);
        Save(*doc, path);
        EXPECT_EQ(L"bg.pdf", PdfFilename(path));

        auto copy = dir / L"copy.xopp";
        Write(*TakeSnapshot(*doc, copy), copy);
        EXPECT_EQ(L"a.xopp.bg.pdf", PdfFilename(copy));
        EXPECT_EQ(path.native() + L".bg.pdf", Load(copy)->PdfPath()->native());

        // Save As, as upstream: the attachment goes along and the new file keeps "attach".
        auto other = dir / L"other.xopp";
        SaveAs(*doc, other);
        EXPECT_TRUE(std::filesystem::exists(other.native() + L".bg.pdf"));
        EXPECT_EQ(L"bg.pdf", PdfFilename(other));
        EXPECT_EQ(other.native() + L".bg.pdf", doc->PdfPath()->native());

        // Another file of that name is never overwritten, nor taken for the attachment: the PDF is named by its path.
        auto third = dir / L"third.xopp";
        WriteFile(third.native() + L".bg.pdf", "someone else's", false);
        SaveAs(*doc, third);
        EXPECT_EQ("someone else's", ReadText(third.native() + L".bg.pdf"));
        EXPECT_EQ(L"other.xopp.bg.pdf", PdfFilename(third));
        auto fourth = dir / L"fourth.xopp";
        SaveAs(*doc, fourth); // the document now names its PDF by path (as the file says): nothing attached appears
        EXPECT_FALSE(std::filesystem::exists(fourth.native() + L".bg.pdf"));
        // Ctrl+S at a renamed place copies nothing along.
        auto renamed = dir / L"renamed.xopp";
        Save(*doc, renamed);
        EXPECT_FALSE(std::filesystem::exists(renamed.native() + L".bg.pdf"));
    }

    TEST(References, PictureBackgroundsStillNameTheirFilesAfterSaveAsAndInsertedPages)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"a.xopp", R"(
<xournal fileversion="4"><title>t</title>
<page width="100" height="100"><background type="pixmap" domain="attach" filename="bg_1.png"/><layer/></page>
<page width="100" height="100"><background type="pixmap" domain="absolute" filename="pics/p.png"/><layer/></page>
<page width="100" height="100"><background type="pixmap" domain="clone" filename="0"/><layer/></page>
</xournal>)");
        WriteBytes(path.native() + L".bg_1.png", "\x01");
        std::filesystem::create_directories(dir / L"pics");
        WriteBytes(dir / L"pics" / L"p.png", "\x01");
        auto doc = Load(path);
        EXPECT_EQ(path.native() + L".bg_1.png", doc->Pages()[2]->BackgroundImagePath()->native());

        // Saved where it was: written back as it was.
        auto same = Backgrounds(*TakeSnapshot(*doc, path));
        EXPECT_EQ(L"attach", same[0]->AttributeOr(L"domain", L""));
        EXPECT_EQ(L"bg_1.png", same[0]->AttributeOr(L"filename", L""));
        EXPECT_EQ(L"pics/p.png", same[1]->AttributeOr(L"filename", L""));

        // Save As in a subfolder with a page inserted first: paths from there, the clone's index follows its page.
        doc->Pages().insert(doc->Pages().begin(), XoppPage::CreateGraph(100, 100));
        auto moved = Backgrounds(*TakeSnapshot(*doc, dir / L"sub" / L"b.xopp"));
        EXPECT_EQ(L"absolute", moved[1]->AttributeOr(L"domain", L""));
        EXPECT_EQ(L"../a.xopp.bg_1.png", moved[1]->AttributeOr(L"filename", L""));
        EXPECT_EQ(L"../pics/p.png", moved[2]->AttributeOr(L"filename", L""));
        EXPECT_EQ(L"clone", moved[3]->AttributeOr(L"domain", L""));
        EXPECT_EQ(L"1", moved[3]->AttributeOr(L"filename", L""));

        // Saved there (Save As): the attached picture goes along, the file keeps "attach" (as upstream).
        std::filesystem::create_directories(dir / L"sub");
        auto saved = dir / L"sub" / L"d.xopp";
        SaveAs(*doc, saved);
        EXPECT_TRUE(std::filesystem::exists(saved.native() + L".bg_1.png"));
        auto savedBackground = ReadXml(saved)->Elements(L"page")[1]->Element(L"background");
        EXPECT_EQ(L"attach", savedBackground->AttributeOr(L"domain", L""));
        EXPECT_EQ(L"bg_1.png", savedBackground->AttributeOr(L"filename", L""));

        // A missing picture keeps being named (from the new place too); a clone whose page is gone names the picture.
        std::filesystem::remove(dir / L"pics" / L"p.png");
        doc = Load(path);
        doc->Pages().erase(doc->Pages().begin());
        auto after = Backgrounds(*TakeSnapshot(*doc, dir / L"sub" / L"c.xopp"));
        EXPECT_EQ(L"../pics/p.png", after[0]->AttributeOr(L"filename", L""));
        EXPECT_EQ(L"absolute", after[1]->AttributeOr(L"domain", L""));
        EXPECT_EQ(L"../a.xopp.bg_1.png", after[1]->AttributeOr(L"filename", L""));
    }
}
