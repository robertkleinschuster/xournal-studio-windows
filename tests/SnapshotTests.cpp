// Ported from XournalStudio.Tests/SnapshotTests.cs: strokes drawn in a session are converted to elements once, and
// every snapshot writes them exactly.

#include "TestSupport.h"

#include "xournal/io/DocumentFile.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppPage.h"

#include <gtest/gtest.h>

#include <thread>

namespace xournal::tests
{
    namespace
    {
        std::shared_ptr<XoppPage> EmptyPage()
        {
            return XoppPage::FromXml(Xml(R"(<page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer/></page>)"));
        }

        std::vector<XmlElementPtr> StrokesOf(XmlElement const& page)
        {
            return page.Element(L"layer")->Elements(L"stroke");
        }

        void ExpectSame(std::vector<XmlElementPtr> const& expected, std::vector<XmlElementPtr> const& actual)
        {
            ASSERT_EQ(expected.size(), actual.size());
            for (size_t i = 0; i < expected.size(); ++i)
            {
                EXPECT_TRUE(expected[i]->DeepEquals(*actual[i])) << "stroke " << i;
            }
        }

        bool StartsWith(std::wstring const& text, std::wstring_view prefix) { return text.starts_with(prefix); }
    }

    TEST(Snapshot, DrawnStrokesAreConvertedOncePerTransformAndWrittenExactly)
    {
        auto page = EmptyPage();
        FakeInk ink;
        auto a = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke>)"));
        auto b = ink.Add(InkOfXml(R"(<stroke tool="highlighter" color="#ffff007f" width="8">5 5 50 50</stroke>)"));

        auto first = page->Ink().SnapshotOf(ink);
        ExpectSame({ Strokes::ToXml(ink.InkOf(a)), Strokes::ToXml(ink.InkOf(b)) }, StrokesOf(*first));
        EXPECT_EQ(2, page->Ink().Conversions());

        auto second = page->Ink().SnapshotOf(ink);
        EXPECT_TRUE(first->DeepEquals(*second));
        EXPECT_EQ(2, page->Ink().Conversions()); // reused, not converted again

        // A snapshot's elements are its own: changing one changes no later snapshot.
        StrokesOf(*second)[0]->SetValue(L"1 1 2 2");
        StrokesOf(*second)[0]->SetAttribute(L"color", L"#000000ff");
        EXPECT_TRUE(first->DeepEquals(*page->Ink().SnapshotOf(ink)));

        // Drawing order is the ink's.
        ink.SetOrder({ b, a });
        ExpectSame({ Strokes::ToXml(ink.InkOf(b)), Strokes::ToXml(ink.InkOf(a)) }, StrokesOf(*page->Ink().SnapshotOf(ink)));
        EXPECT_EQ(2, page->Ink().Conversions());
    }

    TEST(Snapshot, AMovedOrPreviewedStrokeIsWrittenWithItsTransform)
    {
        auto page = EmptyPage();
        FakeInk ink;
        auto a = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>)"));
        page->Ink().SnapshotOf(ink);

        ink.SetTransform(a, Matrix::Translation(100, 0)); // committed move
        auto moved = StrokesOf(*page->Ink().SnapshotOf(ink)).at(0);
        EXPECT_TRUE(Strokes::ToXml(ink.InkOf(a), ink.TransformOf(a))->DeepEquals(*moved));
        EXPECT_TRUE(StartsWith(moved->Value(), L"100 0 "));
        EXPECT_EQ(2, page->Ink().Conversions());

        // A move previewed on it: written with the committed transform until the preview ends.
        page->Ink().BeginPreview(a, ink.TransformOf(a));
        ink.SetTransform(a, Matrix::Translation(300, 0));
        EXPECT_TRUE(StartsWith(StrokesOf(*page->Ink().SnapshotOf(ink)).at(0)->Value(), L"100 0 "));
        EXPECT_EQ(2, page->Ink().Conversions()); // the committed transform is the one converted already
        page->Ink().EndPreview({ a });
        EXPECT_TRUE(StartsWith(StrokesOf(*page->Ink().SnapshotOf(ink)).at(0)->Value(), L"300 0 "));

        // Undo puts the old transform back: the file says so again.
        ink.SetTransform(a, {});
        EXPECT_TRUE(Strokes::ToXml(ink.InkOf(a))->DeepEquals(*StrokesOf(*page->Ink().SnapshotOf(ink)).at(0)));
    }

    TEST(Snapshot, StrokesNoLongerOnTheInkAreLeftOut)
    {
        auto page = EmptyPage();
        FakeInk ink;
        auto a = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>)"));
        auto b = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1">0 5 10 5</stroke>)"));
        page->Ink().SnapshotOf(ink);
        ink.SetOrder({ b });
        ExpectSame({ Strokes::ToXml(ink.InkOf(b)) }, StrokesOf(*page->Ink().SnapshotOf(ink)));
        ink.SetOrder({ a, b });
        ExpectSame({ Strokes::ToXml(ink.InkOf(a)), Strokes::ToXml(ink.InkOf(b)) }, StrokesOf(*page->Ink().SnapshotOf(ink)));
    }

    TEST(Snapshot, AReleasedViewsInkIsWrittenIntoThePageAsASnapshotWritesIt)
    {
        auto page = XoppPage::FromXml(Xml(R"(
<page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>
<stroke tool="pen" color="#000000ff" width="1" other="kept">0 5 10 5</stroke>
<stroke tool="pen" color="#000000ff" width="1" style="dash">0 9 10 9</stroke>
</layer></page>)"));
        auto read = page->Ink().ReadStrokes(); // the dashed one is drawn from the file, not ink
        ASSERT_EQ(2u, read.size());

        FakeInk ink;
        std::vector<std::pair<StrokeId, XmlElementPtr>> attached;
        for (auto const& record : read)
        {
            attached.push_back({ ink.Add(record.ink), record.element });
        }
        page->Ink().Attach(ink, attached);
        auto drawn = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke>)"),
            Matrix::Translation(5, 0));
        auto pasted = ink.Add(InkOfXml(R"(<stroke tool="pen" color="#00ff00ff" width="1" pasted="yes">1 1 2 2</stroke>)"));
        page->Ink().Adopt(pasted, Xml(R"(<stroke tool="pen" color="#00ff00ff" width="1" pasted="yes">1 1 2 2</stroke>)"));
        ink.SetOrder({ attached[1].first, drawn, pasted }); // the first one erased

        auto expected = page->Ink().SnapshotOf(ink);
        page->Ink().Detach();
        EXPECT_TRUE(expected->DeepEquals(*page->Snapshot()));

        // The next view reads exactly these strokes again: the kept one, the drawn one as written, the pasted one.
        auto again = page->Ink().ReadStrokes();
        ASSERT_EQ(3u, again.size());
        EXPECT_EQ(L"kept", again[0].element->AttributeOr(L"other", L""));
        EXPECT_TRUE(StartsWith(again[1].element->Value(), L"5 0 "));
        EXPECT_EQ(L"yes", again[2].element->AttributeOr(L"pasted", L""));
        EXPECT_EQ(1u, page->DrawnStrokes().size()); // the dashed one stays as it is
    }

    class InkOfIsNeedsOwnDrawingThenFromXml : public testing::TestWithParam<char const*> {};

    TEST_P(InkOfIsNeedsOwnDrawingThenFromXml, Agrees)
    {
        auto e = Xml(GetParam());
        auto expected = Strokes::NeedsOwnDrawing(*e) ? std::nullopt : Strokes::FromXml(*e);
        auto actual = Strokes::InkOf(*e);
        ASSERT_EQ(expected.has_value(), actual.has_value());
        if (expected)
        {
            EXPECT_EQ(*expected, *actual);
        }
    }

    INSTANTIATE_TEST_SUITE_P(Snapshot, InkOfIsNeedsOwnDrawingThenFromXml, testing::Values(
        R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2 1 3 4">0 0 10 0 10 10 0 10</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2">5 5</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2"></stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2" fill="128">0 0 10 0 10 10</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2" style="dash">0 0 10 0</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2">0 0 10 NaN</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2 x">0 0 10 0</stroke>)",
        R"(<stroke tool="pen" color="#000000ff" width="2">0 0 10 0 7</stroke>)"));

    TEST(Snapshot, ADocumentLoadedOnAnotherThreadGivesInk)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"t.xopp", R"(<?xml version="1.0" standalone="no"?>
<xournal creator="test" fileversion="4"><page width="100" height="100">
<background type="solid" color="#ffffffff" style="plain"/>
<layer><stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke></layer></page></xournal>)");
        std::unique_ptr<XoppDocument> loaded;
        std::thread([&] { loaded = io::DocumentFile::Load(path); }).join();
        auto strokes = loaded->Pages().at(0)->Ink().ReadStrokes();
        ASSERT_EQ(1u, strokes.size());
        EXPECT_EQ(3u, strokes[0].ink.samples.size());
        EXPECT_NEAR(3 / InkWidth::FullPressureFactor, strokes[0].ink.look.size, 1e-6); // widest segment at full pressure
    }
}
