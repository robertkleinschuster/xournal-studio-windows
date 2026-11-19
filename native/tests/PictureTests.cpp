// Ported from XournalStudio.Tests/PictureTests.cs.

#include "TestSupport.h"

#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppPage.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    namespace
    {
        std::vector<std::wstring> AttributeNames(XmlElement const& e)
        {
            std::vector<std::wstring> names;
            for (auto const& a : e.Attributes())
            {
                names.push_back(a.name);
            }
            return names;
        }
    }

    TEST(Pictures, ImageElementsAreXournalImages)
    {
        std::vector<std::uint8_t> png{ 0x89, 0x50, 0x4e, 0x47 };
        auto e = XoppImage::Create({ 10, 20, 100, 50 }, png, 300, 150);
        EXPECT_EQ(L"image", e->Name());
        EXPECT_EQ((Rect{ 10, 20, 100, 50 }), XoppImage::GetBounds(*e));
        EXPECT_EQ(png, XoppImage::GetData(*e));
        EXPECT_EQ(L"300 150", e->AttributeOr(L"natural_size", L""));
        auto moved = XoppClip::Transformed(*e, Matrix::Translation(20, 20));
        EXPECT_EQ(L"30", moved->AttributeOr(L"left", L""));
        EXPECT_EQ(L"90", moved->AttributeOr(L"bottom", L""));
        EXPECT_EQ(e->Value(), moved->Value());
        EXPECT_EQ(L"10", e->AttributeOr(L"left", L"")); // the original is unchanged
    }

    TEST(Pictures, MovedPicturesKeepAllTheirAttributes)
    {
        auto e = Xml(R"(<teximage text="" left="1" top="2" right="3" bottom="4" natural_size="2 2" portal="abc">JVBERi0=</teximage>)");
        auto moved = XoppClip::Transformed(*e, Matrix::Translation(9, 18));
        EXPECT_EQ((std::vector<std::wstring>{ L"text", L"left", L"top", L"right", L"bottom", L"natural_size", L"portal" }),
            AttributeNames(*moved));
        EXPECT_EQ(L"22", moved->AttributeOr(L"bottom", L""));
        EXPECT_EQ(L"JVBERi0=", moved->Value());
    }

    TEST(Pictures, PagesListAndChangeTheirImages)
    {
        auto page = XoppPage::FromXml(Xml(R"(
<page width="595" height="842"><background type="solid" color="#ffffffff" style="graph"/>
<layer><teximage left="1" top="2" right="3" bottom="4">JVBERi0=</teximage></layer>
<layer><image left="5" top="10" right="50" bottom="60">iVBO</image></layer></page>)"));
        EXPECT_EQ(2u, page->Images().List().size());
        EXPECT_EQ(60, page->ContentBottom());
        EXPECT_TRUE(XoppImage::IsPdf(XoppImage::GetData(*page->Images().List()[0])));

        int changes = 0;
        page->Images().changed = [&](auto const&) { ++changes; };
        std::vector<std::uint8_t> one{ 1 };
        auto added = XoppImage::Create({ 0, 100, 10, 10 }, one, 1, 1);
        page->Images().Add({ added });
        EXPECT_EQ(110, page->ContentBottom());
        page->Images().Remove({ page->Images().List()[0] });
        EXPECT_EQ(2u, page->Images().List().size());
        EXPECT_EQ(2, changes);
        auto last = page->Snapshot()->Elements(L"layer").back()->Elements().back();
        EXPECT_TRUE(added->DeepEquals(*last)); // a copy
        EXPECT_NE(added, last);
    }

    TEST(Pictures, RemovedPicturesComeBackAtTheirPlace)
    {
        auto page = XoppPage::FromXml(Xml(R"(
<page width="100" height="100"><background type="solid" color="#ffffffff" style="graph"/>
<layer><image left="0" top="0" right="1" bottom="1">AA==</image><stroke tool="pen" color="#000000ff" width="1">0 0 1 1</stroke></layer>
<layer name="2"><stroke tool="pen" color="#000000ff" width="1">0 0 2 2</stroke></layer></page>)"));
        auto image = page->Images().List().at(0);
        auto places = page->Images().Remove({ image });
        EXPECT_TRUE(page->Images().List().empty());
        page->Images().Restore(places);
        auto layer = page->Snapshot()->Element(L"layer");
        EXPECT_EQ(L"image", layer->Elements()[0]->Name()); // first in the first layer again, under the stroke
    }

    TEST(Pictures, PicturesPlacedByAMatrixKeepItWhenMoved)
    {
        // Version 5: natural size 200x100 px, scaled by 0.5 and turned by 90 degrees, at 300,50.
        auto e = Xml(R"(<image natural_size="200 100" matrix="0 0.5 -0.5 0 300 50">AA==</image>)");
        EXPECT_EQ((Rect{ 250, 50, 50, 100 }), XoppImage::GetBounds(*e));
        EXPECT_EQ((Size{ 200, 100 }), XoppImage::GetPlacement(*e).size);

        XoppImage::SetBounds(*e, { 260, 70, 50, 100 }); // moved by 10,20
        EXPECT_EQ((Rect{ 260, 70, 50, 100 }), XoppImage::GetBounds(*e));
        EXPECT_EQ(nullptr, e->Attribute(L"left")); // still placed by its matrix
        EXPECT_EQ(L"0 0.5 -0.5 0 310 70", e->AttributeOr(L"matrix", L""));

        auto legacy = Xml(R"(<image left="10" top="20" right="110" bottom="70">AA==</image>)");
        auto p = XoppImage::GetPlacement(*legacy);
        EXPECT_EQ((Size{ 100, 50 }), p.size);
        EXPECT_EQ((Point{ 10, 20 }), p.transform.Translation());
    }
}
