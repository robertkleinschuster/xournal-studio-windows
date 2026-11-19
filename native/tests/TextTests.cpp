// Ported from XournalStudio.Tests/FileFormatTests.cs: texts and where pasted content goes.

#include "TestSupport.h"

#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppPage.h"
#include "xournal/model/XoppText.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    TEST(Texts, TextIsReadAsUpstreamWritesIt)
    {
        auto legacy = XoppText::Read(*Xml(R"(<text font="Sans Bold" size="14" x="10" y="20" color="#ff0000ff" wrap="150" align="center">Zeile 1
Zeile 2</text>)"));
        EXPECT_EQ(L"Sans", legacy.family);
        EXPECT_EQ(L"Arial", legacy.WindowsFamily());
        EXPECT_TRUE(legacy.bold);
        EXPECT_EQ(14, legacy.size);
        EXPECT_EQ(150, legacy.wrap.value_or(0));
        EXPECT_EQ(TextAlignment::Center, legacy.alignment);
        EXPECT_EQ((Point{ 10, 20 }), legacy.transform.Translation());
        EXPECT_EQ(L"Zeile 1\nZeile 2", legacy.content);

        auto placed = XoppText::Read(*Xml(R"(<text font="Serif" size="12" matrix="0 1 -1 0 50 60" color="#000000ff">x</text>)"));
        EXPECT_EQ((Matrix{ 0, 1, -1, 0, 50, 60 }), placed.transform); // turned by 90 degrees
        EXPECT_FALSE(placed.wrap);
    }

    TEST(Texts, PastedContentGoesBelowTextsAndDrawnStrokes)
    {
        auto page = XoppPage::FromXml(Xml(R"(
<page width="200" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#000000ff" width="2" style="dash">0 50 90 50</stroke>
<text font="Sans" size="10" x="10" y="100" color="#000000ff">a
b</text>
</layer></page>)"));
        EXPECT_NEAR(124, page->ContentBottom(), 1e-3); // two lines of 1.2 x 10 below y = 100
        EXPECT_TRUE(XoppText::Read(*Xml(R"(<text font="Sans oblique" size="10" x="0" y="0">x</text>)")).italic);

        // Wrapped: about (estimated width / wrap) lines, never more than one per character, no overflow.
        auto wrapped = [](std::string wrap, std::string content)
        {
            return XoppPage::FromXml(Xml(R"(<page width="200" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer><text font="Sans" size="10" x="0" y="0" wrap=")" +
                wrap + "\">" + content + "</text></layer></page>"));
        };
        EXPECT_NEAR(3 * 12, wrapped("100", std::string(40, 'x'))->ContentBottom(), 1e-3); // 40 x 6 pt = 240 pt: 3 lines
        EXPECT_NEAR(11 * 12, wrapped("1e-6", "Hello World")->ContentBottom(), 1e-3);
        EXPECT_EQ(3u, XoppText::Read(*Xml("<text size=\"10\" x=\"0\" y=\"0\">a b c</text>")).Paragraphs().size());
        EXPECT_EQ(1u, XoppText::CharacterCount(L"é")); // a letter with its accent is one character
    }

    TEST(Texts, InvalidNumbersOfTextsFallBack)
    {
        auto text = XoppText::Read(*Xml(R"(<text font="Sans" size="0" x="1e300" y="5" matrix="1 0 0 1 NaN 5">x</text>)"));
        EXPECT_EQ(12, text.size);                                // a font size must be positive
        EXPECT_EQ((Point{ 0, 5 }), text.transform.Translation()); // matrix with NaN ignored, x beyond the limit
        EXPECT_FALSE(XoppFormat::ReadMatrix(*Xml(R"(<image matrix="1 0 0 1 Infinity 0"/>)")));
        EXPECT_EQ(12, XoppText::Read(*Xml(R"(<text size="-3" x="0" y="0">x</text>)")).size);
        auto huge = XoppText::Read(*Xml(R"(<text size="1e300" wrap="1e300" x="0" y="0">x</text>)"));
        EXPECT_EQ(12, huge.size);
        EXPECT_FALSE(huge.wrap);
        auto page = XoppPage::FromXml(Xml(R"(
<page width="100" height="100"><background type="solid" color="#ffffffff" style="plain"/><layer>
<image left="NaN" top="0" right="10" bottom="20">AAAA</image>
<image left="0" top="0" right="1" bottom="1" natural_size="1 1" matrix="3e38 0 0 3e38 0 0">AAAA</image>
</layer></page>)"));
        EXPECT_EQ(20, page->ContentBottom()); // the hostile matrix is ignored for placing pasted content
        EXPECT_EQ(12, XoppText::Read(*Xml(R"(<text size="900000" x="0" y="0">x</text>)")).size);
    }

    TEST(Texts, TheTextToolWritesTextsAsUpstreamAndChangesOnlyWhatWasEdited)
    {
        auto created = XoppText::Create({ L"a\nb", L"Arial", 14, Color{ 0xFF, 0xFF, 0, 0 }, { 10.5, 20 } });
        EXPECT_EQ(L"text", created->Name());
        EXPECT_EQ(L"Sans", *created->Attribute(L"font")); // Xournal++'s default family
        EXPECT_EQ(L"14", *created->Attribute(L"size"));
        EXPECT_EQ(L"10.5", *created->Attribute(L"x"));
        EXPECT_EQ(L"#ff0000ff", *created->Attribute(L"color"));
        EXPECT_EQ(L"a\nb", created->Value());

        auto text = Xml(R"(<text font="Sans Bold" size="12" x="10" y="20" color="#000000ff" wrap="100" ts="7">x</text>)");
        auto edit = XoppText::Read(*text).ToEdit();
        edit.content = L"y\nz";
        XoppText::Apply(*text, edit);
        EXPECT_EQ(L"Sans Bold", *text->Attribute(L"font")); // the family stayed: the description too
        EXPECT_EQ(L"7", *text->Attribute(L"ts"));           // another program's attribute
        EXPECT_EQ(L"y\nz", text->Value());
        edit.windowsFamily = L"Consolas";
        edit.at = { 15, 20 };
        XoppText::Apply(*text, edit);
        EXPECT_EQ(L"Consolas", *text->Attribute(L"font"));
        EXPECT_EQ(L"15", *text->Attribute(L"x"));
    }

    TEST(Texts, MovingAndScalingATextMovesItsCornerAndScalesItsSize)
    {
        auto text = Xml(R"(<text font="Sans" size="10" x="10" y="20" wrap="100">x</text>)");
        XoppText::Transform(*text, Matrix::Scale(2, Point{ 0, 0 }) * Matrix::Translation(5, 0));
        auto moved = XoppText::Read(*text);
        EXPECT_EQ((Point{ 25, 40 }), moved.transform.Translation());
        EXPECT_EQ(20, moved.size);
        EXPECT_EQ(200, moved.wrap.value_or(0));

        auto placed = Xml(R"(<text size="12" matrix="0 1 -1 0 50 60">x</text>)");
        XoppText::Transform(*placed, Matrix::Translation(1, 2));
        EXPECT_EQ((Matrix{ 0, 1, -1, 0, 51, 62 }), XoppText::Read(*placed).transform);
        EXPECT_EQ(12, XoppText::Read(*placed).size); // the matrix carries any scale

        auto crlf = Xml("<text size=\"12\" x=\"0\" y=\"0\">a&#13;&#10;b&#13;c</text>");
        EXPECT_EQ(L"a\nb\nc", XoppText::Read(*crlf).ToEdit().content);
    }
}
