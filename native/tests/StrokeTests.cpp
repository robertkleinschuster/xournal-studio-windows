// Ported from XournalStudio.Tests/FileFormatTests.cs: strokes between the file and the ink, drawn strokes.

#include "TestSupport.h"

#include "xournal/base/Text.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppFormat.h"

#include <gtest/gtest.h>

#include <cmath>

namespace xournal::tests
{
    namespace
    {
        std::vector<double> Widths(XmlElement const& stroke, int decimals = 2)
        {
            std::vector<double> widths;
            std::wstring text = stroke.AttributeOr(L"width", L"");
            for (auto part : SplitWhitespace(text))
            {
                double scale = std::pow(10.0, decimals);
                widths.push_back(std::round(*XoppFormat::ParseDouble(part) * scale) / scale);
            }
            return widths;
        }

        std::optional<StrokeShape> Shape(std::string_view xml) { return Strokes::ReadShape(*Xml(xml)); }

        std::vector<double> Dashes(std::wstring style) { return Strokes::Dashes(&style); }
    }

    TEST(Strokes, RoundTripKeepsPointsColorAndWidths)
    {
        auto ink = InkOfXml(R"(<stroke tool="pen" color="#ff0000ff" width="2 1.5 1.8 1.2">1.5 2.5 10 20 30 40 50 60</stroke>)");
        auto back = Strokes::ToXml(ink);
        EXPECT_EQ(L"pen", back->AttributeOr(L"tool", L""));
        EXPECT_EQ(L"#ff0000ff", back->AttributeOr(L"color", L""));
        EXPECT_EQ(L"1.5 2.5 10 20 30 40 50 60", back->Value());
        EXPECT_EQ((std::vector<double>{ 2, 1.5, 1.8, 1.2 }), Widths(*back));
        // Xournal's width is the width at full pressure: the ink draws at most that wide.
        EXPECT_NEAR(2 / InkWidth::FullPressureFactor, ink.look.size, 1e-6);
    }

    TEST(Strokes, SegmentsWiderThanTheToolWidthKeepTheirWidths)
    {
        // Strokes written before 2026-11-18 (Windows ink's size as the tool width) have segments up to 1.75 times wider.
        auto back = Strokes::ToXml(InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1 0.7 1.2 1.6">0 0 10 0 20 0 30 0</stroke>)"));
        EXPECT_EQ((std::vector<double>{ 1.6, 0.7, 1.2, 1.6 }), Widths(*back));
    }

    TEST(Strokes, AreDrawnAsPolylines)
    {
        // A rectangle (resistor) of five points must keep its corners, like in Xournal++: every point is a sample.
        auto ink = InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1.41">10 10 50 10 50 30 10 30 10 10</stroke>)");
        EXPECT_EQ(5u, ink.samples.size());
        EXPECT_TRUE(ink.look.ignorePressure);
        EXPECT_EQ(2u, InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="2">5 5</stroke>)").samples.size()); // a dot
    }

    TEST(Strokes, HighlighterKeepsXournalAlpha)
    {
        auto back = Strokes::ToXml(InkOfXml(R"(<stroke tool="highlighter" color="#ffff007f" width="8.5">1 2 3 4</stroke>)"));
        EXPECT_EQ(L"highlighter", back->AttributeOr(L"tool", L""));
        EXPECT_EQ(L"#ffff007f", back->AttributeOr(L"color", L""));
        EXPECT_EQ(L"8.5", back->AttributeOr(L"width", L""));
    }

    TEST(Strokes, APreviewedMoveIsNotWrittenTheCommittedTransformIs)
    {
        auto ink = InkOfXml(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>)");
        EXPECT_TRUE(Strokes::ToXml(ink, {})->Value().starts_with(L"0 0 "));
        EXPECT_TRUE(Strokes::ToXml(ink, Matrix::Translation(100, 0))->Value().starts_with(L"100 0 "));
    }

    TEST(Strokes, DamagedNumbersFollowTheFilesRule)
    {
        // The points end at the first damaged number: point i keeps the width of segment i.
        auto damaged = Shape(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 10 20 NaN 30 30</stroke>)");
        EXPECT_EQ(2u, damaged->points.size());
        EXPECT_FALSE(Shape(R"(<stroke tool="pen" color="#000000ff" width="1">1e400 0</stroke>)"));

        // Moving a damaged stroke moves exactly the points shown and keeps the rest as read (never deleted).
        auto stroke = Xml(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 10 10 20 NaN 30 30</stroke>)");
        Strokes::Transform(*stroke, Matrix::Translation(5, 0));
        EXPECT_EQ(L"5 0 15 10 20 NaN 30 30", stroke->Value());
        EXPECT_TRUE(Strokes::NeedsOwnDrawing(*Xml(R"(<stroke style="cust: -3 2">0 0 1 1</stroke>)"))); // never rewritten as ink
        EXPECT_EQ((std::vector<double>{ 0, 3 }), Dashes(L"cust: 0 3")); // zero-length dashes: dots, as upstream
        EXPECT_TRUE(Dashes(L"cust: 0 0").empty());
        EXPECT_TRUE(Strokes::NeedsOwnDrawing(*Xml(R"(<stroke style="cust:3 2">0 0 1 1</stroke>)"))); // kept, never rewritten
        EXPECT_TRUE(Dashes(L"cust: 3 0").empty()); // no gap: solid, on screen and in the export
        EXPECT_EQ((std::vector<double>{ 3 }), Dashes(L"cust: 3")); // repeats as 3 on, 3 off
        EXPECT_TRUE(Strokes::NeedsOwnDrawing(*stroke)); // damaged: drawn from the file, never ink or moved
        EXPECT_EQ(1, Shape(R"(<stroke tool="pen" color="#000000ff" width="0">0 0 5 5</stroke>)")->width); // as the ink
        EXPECT_EQ(0, Shape(R"(<stroke tool="pen" color="#000000ff" width="0" fill="128">0 0 5 5 0 5</stroke>)")->width); // filling only
        EXPECT_EQ(1, Shape(R"(<stroke tool="pen" color="#000000ff" width="0">0 0 10 10 20 NaN</stroke>)")->width); // damaged: as the ink did
        EXPECT_FALSE(Shape(R"(<stroke style="dash" color="#000000ff" width="2 0 0">0 0 5 5 9 9</stroke>)")->Bottom()); // no segment drawn
        EXPECT_FALSE(Shape(R"(<stroke fill="128" color="#000000ff" width="0">0 0 5 5</stroke>)")->Bottom()); // no area to fill
        EXPECT_FALSE(Shape(R"(<stroke capStyle="butt" color="#000000ff" width="2">50 700</stroke>)")->Bottom()); // a butt dot is nothing
        EXPECT_EQ(11, *Shape(R"(<stroke style="dash" color="#000000ff" width="2 2 0">0 0 10 10 10 800</stroke>)")->Bottom()); // only drawn segments
        // Square ends: sqrt(2) * w/2 below an end, w/2 below an inner point - any point of a dashed line may be a dash end.
        EXPECT_NEAR(11, *Shape(R"(<stroke capStyle="square" color="#000000ff" width="2">0 0 10 10 20 0</stroke>)")->Bottom(), 1e-6);
        EXPECT_NEAR(10 + std::sqrt(2.0),
            *Shape(R"(<stroke capStyle="square" style="dash" color="#000000ff" width="2">0 0 10 10 20 0</stroke>)")->Bottom(), 1e-6);
        // Dashes that cannot be drawn at this width are solid (DashesFor nothing) - the rule drawing and Bottom() share.
        auto solid = Shape(R"(<stroke capStyle="square" style="cust: 900000 1" color="#000000ff" width="1e-303">0 0 10 10 20 0</stroke>)");
        EXPECT_FALSE(solid->DashesFor(solid->width));
        EXPECT_TRUE(Dashes(L"cust: -3 2").empty());
    }

    TEST(Strokes, ColorsAsXournalWritesThem)
    {
        EXPECT_EQ((Color{ 0x7f, 0xff, 0xff, 0 }), ParseColor(L"#ffff007f", Colors::Black));
        EXPECT_EQ((Color{ 0xff, 0x12, 0x34, 0x56 }), ParseColor(L"#123456", Colors::Black));
        EXPECT_EQ((Color{ 0xff, 0x33, 0x33, 0xcc }), ParseColor(L"blue", Colors::Black));
        EXPECT_EQ(Colors::White, ParseColor(L"#zzzzzz", Colors::White));
        EXPECT_EQ(L"#3333ccff", FormatColor({ 0xff, 0x33, 0x33, 0xcc }));
    }
}
