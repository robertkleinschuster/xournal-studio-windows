// Ported from XournalStudio.Tests/InsertPdfPagesTests.cs (the model part; appending PDF pages is a later PDF service).

#include "TestSupport.h"

#include "xournal/io/DocumentFile.h"
#include "xournal/model/CopyPages.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    TEST(CopyPages, PagesOfAnotherDocumentAreCopiedWithEverythingAndReferToTheTargetsPdfPages)
    {
        TempFolder temp;
        auto pdf = temp / L"skript.pdf";
        WriteFile(pdf, "%PDF-1.7", false);
        auto source = XoppDocument::FromPdf(pdf, { { 500, 842 }, { 510, 842 }, { 520, 842 } });
        auto text = XmlElement::Make(L"text", { { L"unknown", L"kept" } });
        text->SetValue(L"Hallo");
        source->Pages()[2]->AddToTopLayer({ text });
        auto sourcePath = temp / L"quelle.xopp";
        io::DocumentFile::Save(*source, sourcePath);
        auto loaded = io::DocumentFile::Load(sourcePath);

        EXPECT_EQ((std::vector<int>{ 0, 2 }), loaded->PdfPagesOf({ 2, 0 }));
        auto copies = CopyPages(*loaded, { 2, 0 }, [](int p) { return p + 10; }); // appended at page 11 of the target's PDF

        ASSERT_EQ(2u, copies.size());
        EXPECT_EQ(12, copies[0]->PdfPageIndex());
        EXPECT_EQ(10, copies[1]->PdfPageIndex());
        for (auto const& copy : copies)
        {
            EXPECT_EQ(nullptr, copy->Background().Attribute(L"filename")); // only the target's first PDF page names it
        }
        ASSERT_EQ(1u, copies[0]->Texts().size());
        EXPECT_EQ(L"Hallo", copies[0]->Texts()[0]->Value());
        EXPECT_EQ(L"kept", copies[0]->Texts()[0]->AttributeOr(L"unknown", L"")); // nothing is lost on the way
        EXPECT_EQ(2, loaded->Pages()[2]->PdfPageIndex()); // the source is untouched
        EXPECT_NE(nullptr, loaded->Pages()[0]->Background().Attribute(L"filename"));
    }
}
