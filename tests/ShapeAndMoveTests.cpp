// Ported from XournalStudio.Tests/ShapeAndMoveTests.cs.

#include "TestSupport.h"

#include "xournal/model/BackgroundRuling.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppImage.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    TEST(ShapeAndMove, TranslateMovesPointsAndKeepsUnknownAttributes)
    {
        auto e = Xml(R"(<stroke tool="pen" color="#000000ff" width="1.41 1.2" fork-extra="keep">10 20 30.5 40</stroke>)");
        Strokes::Transform(*e, Matrix::Translation(5, -2.5));
        EXPECT_EQ(L"15 17.5 35.5 37.5", e->Value());
        EXPECT_EQ(L"keep", e->AttributeOr(L"fork-extra", L""));
        EXPECT_EQ(L"1.41 1.2", e->AttributeOr(L"width", L""));
    }

    TEST(ShapeAndMove, ScalingAboutACornerScalesPointsButNotWidths)
    {
        auto e = Xml(R"(<stroke tool="pen" color="#000000ff" width="1.41 1.2">10 10 30 20</stroke>)");
        Matrix m = Matrix::Scale(2, { 10, 10 });
        Strokes::Transform(*e, m);
        EXPECT_EQ(L"10 10 50 30", e->Value());
        EXPECT_EQ(L"1.41 1.2", e->AttributeOr(L"width", L""));
        auto odd = Xml("<stroke>1 2 3</stroke>");
        Strokes::Transform(*odd, Matrix::Translation(1, 1));
        EXPECT_EQ(L"2 3 3", odd->Value()); // a stray number stays
        EXPECT_EQ((Rect{ 10, 10, 40, 20 }), XoppImage::Transform({ 10, 10, 20, 10 }, m));
    }

    TEST(ShapeAndMove, SetBoundsMovesThePictureInPlace)
    {
        auto layer = Xml(R"(<layer><image left="1" top="2" right="11" bottom="22" extra="x">AAAA</image><stroke/></layer>)");
        auto image = layer->Element(L"image");
        XoppImage::SetBounds(*image, { 6, 7, 10, 20 });
        EXPECT_EQ(image, layer->Elements()[0]); // same element, same place in the layer
        EXPECT_EQ((Rect{ 6, 7, 10, 20 }), XoppImage::GetBounds(*image));
        EXPECT_EQ(L"x", image->AttributeOr(L"extra", L""));
    }

    TEST(ShapeAndMove, ClipIsRelativeToItsCornerAndPlacesScaled)
    {
        auto stroke = Xml(R"(<stroke tool="pen" color="#000000ff" width="1.41 1.2" extra="x">110 220 130 240</stroke>)");
        auto image = Xml(R"(<image left="100" top="200" right="140" bottom="220">AAAA</image>)");
        auto clip = XoppClip::Create({ 100, 200, 40, 40 }, { image, stroke });

        EXPECT_EQ((Size{ 40, 40 }), XoppClip::GetSize(*clip));
        EXPECT_EQ(L"110 220 130 240", stroke->Value()); // the originals stay where they are

        auto placed = XoppClip::Place(*clip, Matrix::Scale(0.5, 0.5) * Matrix::Translation(14, 28));
        EXPECT_EQ((Rect{ 14, 28, 20, 10 }), XoppImage::GetBounds(*placed[0]));
        EXPECT_EQ(L"19 38 29 48", placed[1]->Value());
        EXPECT_EQ(L"x", placed[1]->AttributeOr(L"extra", L""));
        EXPECT_EQ(L"1.41 1.2", placed[1]->AttributeOr(L"width", L""));
    }

    TEST(ShapeAndMove, RulingsFollowUpstreamsBackgroundViews)
    {
        constexpr double W = 595.27559, H = 841.88976;
        Color paper = Colors::White;
        auto graph = BackgroundRuling::For(BackgroundStyle::Graph, nullptr, paper, W, H);
        ASSERT_EQ(1u, graph.size());
        EXPECT_EQ(41u + 59u, graph[0].lines.size()); // 5 mm squares, none on the sheet's edge
        EXPECT_EQ(0.5, graph[0].width);

        auto ruled = BackgroundRuling::For(BackgroundStyle::Ruled, nullptr, paper, W, H);
        ASSERT_EQ(1u, ruled.size());
        EXPECT_EQ(80, ruled[0].lines.front().first.y);
        EXPECT_EQ(80 + 29 * 24, ruled[0].lines.back().first.y); // header 80, footer 60, 24 apart

        std::wstring negativeMargin = L"m1=-72";
        auto lined = BackgroundRuling::For(BackgroundStyle::Lined, &negativeMargin, paper, W, H);
        ASSERT_EQ(2u, lined.size());
        ASSERT_EQ(1u, lined[1].lines.size());
        EXPECT_NEAR(W - 72, lined[1].lines[0].first.x, 1e-3); // negative margin: on the right
        EXPECT_EQ(255, lined[1].color.r);                     // deeppink

        std::wstring boldConfig = L"m1=40,rm=1,bli=5,blw=1.2";
        auto bold = BackgroundRuling::For(BackgroundStyle::Graph, &boldConfig, paper, W, H);
        ASSERT_EQ(2u, bold.size());
        EXPECT_EQ(1.2, bold[1].width);
        for (auto const& ruling : bold)
        {
            for (auto const& [from, to] : ruling.lines)
            {
                EXPECT_TRUE(from.x >= 40 - 1e-9 && to.x <= W - 40 + 1e-9);
            }
        }

        auto isoDotted = BackgroundRuling::For(BackgroundStyle::IsoDotted, nullptr, paper, W, H);
        ASSERT_EQ(1u, isoDotted.size());
        EXPECT_FALSE(isoDotted[0].dots.empty());
        auto isoGraph = BackgroundRuling::For(BackgroundStyle::IsoGraph, nullptr, paper, W, H);
        ASSERT_EQ(1u, isoGraph.size());
        EXPECT_FALSE(isoGraph[0].lines.empty());
        auto staves = BackgroundRuling::For(BackgroundStyle::Staves, nullptr, paper, W, H);
        ASSERT_EQ(1u, staves.size());
        EXPECT_EQ(12u * 7u, staves[0].lines.size()); // 5 lines and 2 bars per staff
        EXPECT_TRUE(BackgroundRuling::For(BackgroundStyle::Plain, nullptr, paper, W, H).empty());

        auto dark = BackgroundRuling::For(BackgroundStyle::Graph, nullptr, { 255, 30, 30, 30 }, W, H);
        ASSERT_EQ(1u, dark.size());
        EXPECT_EQ(0x43, dark[0].color.r); // alt color

        std::wstring red = L"f1=0xff0000";
        auto custom = BackgroundRuling::For(BackgroundStyle::Ruled, &red, paper, 100, 300);
        EXPECT_EQ((Color{ 255, 255, 0, 0 }), custom[0].color);
    }
}
