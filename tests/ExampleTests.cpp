// The sanitized real documents in examples\ load and save without losing anything: the saved XML tree equals the
// loaded one (apart from formatting whitespace and the creator, which every save writes), also after the pages' strokes
// went through an ink view and back.

#include "TestSupport.h"

#include "xournal/io/DocumentFile.h"
#include "xournal/io/Files.h"
#include "xournal/io/Gzip.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    namespace
    {
        class ExampleDocument : public testing::TestWithParam<wchar_t const*> {};

        // The root's creator is the one attribute a save changes.
        XmlElementPtr WithoutCreator(XmlElement const& root)
        {
            auto copy = Normalized(root);
            copy->RemoveAttribute(L"creator");
            return copy;
        }

        void ExpectSameTree(XmlElement const& expected, XmlElement const& actual)
        {
            auto a = WithoutCreator(expected), b = WithoutCreator(actual);
            ASSERT_EQ(a->Elements().size(), b->Elements().size());
            for (size_t i = 0; i < a->Elements().size(); ++i)
            {
                EXPECT_TRUE(a->Elements()[i]->DeepEquals(*b->Elements()[i])) << "element " << i;
            }
            EXPECT_TRUE(a->DeepEquals(*b));
        }
    }

    TEST_P(ExampleDocument, LoadsAndSavesWithoutLosingAnything)
    {
        std::filesystem::path example = std::filesystem::path(XOURNAL_EXAMPLES_DIR) / GetParam();
        TempFolder temp;
        auto file = temp / example.filename().native();
        std::filesystem::copy_file(example, file);
        std::filesystem::copy_file(example.parent_path() / example.filename().replace_extension(L".pdf"),
            temp / example.filename().replace_extension(L".pdf").native());

        auto before = io::DocumentFile::ReadXml(file);
        auto doc = io::DocumentFile::Load(file);
        EXPECT_GT(doc->Pages().size(), 0u);
        ASSERT_TRUE(doc->PdfPath());
        EXPECT_TRUE(std::filesystem::exists(*doc->PdfPath()));
        io::DocumentFile::Save(*doc, file);
        ExpectSameTree(*before, *io::DocumentFile::ReadXml(file));
        // Upstream's layout: gzip, the declaration, each page on a line of its own.
        auto saved = io::Files::ReadAll(file);
        ASSERT_TRUE(io::Gzip::IsGzip(saved));
        auto xml = io::Gzip::Decompress(saved);
        std::string text(xml.begin(), xml.end());
        EXPECT_TRUE(text.starts_with("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>")) << text.substr(0, 200);
        size_t pageLines = 0;
        for (size_t at = text.find("\n<page "); at != std::string::npos; at = text.find("\n<page ", at + 1))
        {
            ++pageLines;
        }
        EXPECT_EQ(doc->Pages().size(), pageLines);

        // Every page through an ink view and back: unchanged strokes stay their original elements.
        doc = io::DocumentFile::Load(file);
        std::vector<std::unique_ptr<FakeInk>> views;
        for (auto const& page : doc->Pages())
        {
            auto& ink = *views.emplace_back(std::make_unique<FakeInk>());
            std::vector<std::pair<StrokeId, XmlElementPtr>> attached;
            for (auto const& record : page->Ink().ReadStrokes())
            {
                attached.push_back({ ink.Add(record.ink), record.element });
            }
            page->Ink().Attach(ink, attached);
        }
        io::DocumentFile::Save(*doc, file);
        ExpectSameTree(*before, *io::DocumentFile::ReadXml(file));
        for (auto const& page : doc->Pages())
        {
            page->Ink().Detach();
        }
        io::DocumentFile::Save(*doc, file);
        ExpectSameTree(*before, *io::DocumentFile::ReadXml(file));
    }

    INSTANTIATE_TEST_SUITE_P(Examples, ExampleDocument, testing::Values(L"Schaltungen.xopp", L"Grenzwerte.xopp"));
}
