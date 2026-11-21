// Compatibility with what upstream Xournal++ reads and writes (src/core/control/xojfile): MrWriter's pressures, audio
// attributes, layer and background names, old Xournal's paper colors, picture data of any format.

#include "TestSupport.h"

#include "xournal/base/Text.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/model/CopyPages.h"
#include "xournal/model/Snapshot.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"
#include "xournal/model/XoppPage.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using namespace io::DocumentFile;

    TEST(FormatCompatibility, RotatedPicturesAndTextKeepTheirAffinePlacementOnSave)
    {
        auto doc = XoppDocument::FromXml(Xml(R"(<xournal fileversion="4"><page width="100" height="100"><layer>
            <image left="10" top="20" right="30" bottom="60" natural_size="10 20"/>
            <text x="10" y="20" size="12">test</text></layer></page></xournal>)"), {});
        auto layer = doc->Pages().front()->Layers().front();
        auto image = layer->Element(L"image");
        auto text = layer->Element(L"text");
        Matrix rotate{ 0, 1, -1, 0, 80, 0 };
        XoppImage::Transform(*image, rotate, XoppImage::NaturalSize(*image));
        XoppText::Transform(*text, rotate);
        auto snapshot = TakeSnapshot(*doc, {});
        EXPECT_EQ(L"5", snapshot->AttributeOr(L"fileversion", L""));
        auto saved = snapshot->Element(L"page")->Element(L"layer");
        EXPECT_EQ((Rect{ 20, 10, 40, 20 }), XoppImage::GetBounds(*saved->Element(L"image")));
        EXPECT_EQ((Point{ 20, 30 }), XoppImage::GetPlacement(*saved->Element(L"image")).transform.Apply({ 10, 20 }));
        EXPECT_EQ((Point{ 60, 11 }), XoppText::Read(*saved->Element(L"text")).transform.Apply({ 1, 0 }));
    }

    namespace
    {
        constexpr char const* MrWriterStroke =
            R"(<stroke tool="pen" color="#000000ff" width="2" pressures="1 2 3 4">0 0 10 0 20 0 30 0</stroke>)";

        // A page shown with all its strokes in an in-memory ink layer.
        struct ShownPage
        {
            std::shared_ptr<XoppPage> page;
            FakeInk ink;
            std::vector<StrokeId> ids;

            explicit ShownPage(std::shared_ptr<XoppPage> p) : page(std::move(p))
            {
                std::vector<std::pair<StrokeId, XmlElementPtr>> strokes;
                for (auto& record : page->Ink().ReadStrokes())
                {
                    ids.push_back(ink.Add(record.ink));
                    strokes.emplace_back(ids.back(), record.element);
                }
                ink.SetOrder(ids);
                page->Ink().Attach(ink, strokes);
            }
        };

        std::shared_ptr<XoppPage> PageWith(std::string const& layerContent, std::string const& layerAttributes = "")
        {
            return XoppPage::FromXml(Xml(R"(<page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer)" +
                layerAttributes + ">" + layerContent + "</layer></page>"));
        }
    }

    // --- MrWriter's "pressures" -------------------------------------------------------------------------------------

    TEST(FormatCompatibility, MrWriterPressuresAreTheWidthsAsUpstreamReadsThem)
    {
        // Upstream: the first number of "width" is the width, "pressures" the per-point widths; the last point's
        // pressure has no segment and is dropped.
        auto mrWriter = Xml(MrWriterStroke);
        auto xournal = Xml(R"(<stroke tool="pen" color="#000000ff" width="2 1 2 3">0 0 10 0 20 0 30 0</stroke>)");
        EXPECT_EQ(Strokes::InkOf(*xournal), Strokes::InkOf(*mrWriter));
        EXPECT_FALSE(Strokes::InkOf(*mrWriter)->look.ignorePressure);
        EXPECT_EQ((std::vector<double>{ 1, 2, 3 }), Strokes::ReadShape(*mrWriter)->segmentWidths);

        // Further numbers of "width" are not read when "pressures" is there.
        auto both = Xml(R"(<stroke tool="pen" color="#000000ff" width="2 9 9 9" pressures="1 2 3 4">0 0 10 0 20 0 30 0</stroke>)");
        EXPECT_EQ(Strokes::InkOf(*xournal), Strokes::InkOf(*both));

        // A damaged pressure: shown from the file, never rewritten as ink.
        EXPECT_TRUE(Strokes::NeedsOwnDrawing(*Xml(R"(<stroke tool="pen" color="#000000ff" width="2" pressures="1 x">0 0 10 0</stroke>)")));
        EXPECT_FALSE(Strokes::NeedsOwnDrawing(*mrWriter));
    }

    TEST(FormatCompatibility, AnUnchangedMrWriterStrokeIsWrittenAsReadAndConvertedInkAsUpstreamWritesIt)
    {
        ShownPage shown(PageWith(MrWriterStroke));
        auto written = shown.page->Ink().SnapshotOf(shown.ink)->Element(L"layer")->Element(L"stroke");
        EXPECT_TRUE(Xml(MrWriterStroke)->DeepEquals(*written));

        // Moved: only the points change, "pressures" stays (upstream reads it the same way).
        shown.page->Ink().RecordTransform(shown.ids[0], Matrix::Translation(5, 0));
        auto moved = shown.page->Ink().ElementOf(shown.ids[0]);
        EXPECT_EQ(L"5 0 15 0 25 0 35 0", moved->Value());
        EXPECT_EQ(L"1 2 3 4", moved->AttributeOr(L"pressures", L""));

        // Ink made into an element again (a piece of a partly erased stroke): upstream's SaveHandler form.
        auto converted = Strokes::ToXml(*Strokes::InkOf(*Xml(MrWriterStroke)));
        EXPECT_EQ(nullptr, converted->Attribute(L"pressures"));
        EXPECT_EQ(4u, SplitWhitespace(converted->AttributeOr(L"width", L"")).size()); // width + one per segment
    }

    TEST(FormatCompatibility, AMrWriterDocumentIsReadAndSavedAsXournal)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"notes.moj", std::string(R"(<?xml version="1.0"?><MrWriter version="0.0.3">
<page width="595" height="842"><background type="solid" color="#ffffffff" style="plain"/><layer>)") + MrWriterStroke +
            "</layer></page></MrWriter>");
        auto doc = Load(path);
        EXPECT_EQ(L"xournal", doc->Root().Name());
        ASSERT_EQ(1u, doc->Pages().size());
        auto strokes = doc->Pages()[0]->Ink().ReadStrokes();
        ASSERT_EQ(1u, strokes.size());
        EXPECT_FALSE(strokes[0].ink.look.ignorePressure);

        auto saved = dir / L"notes.xopp";
        Save(*doc, saved);
        auto xml = ReadXml(saved);
        EXPECT_EQ(L"xournal", xml->Name());
        EXPECT_EQ(L"0.0.3", xml->AttributeOr(L"version", L""));  // kept
        EXPECT_EQ(L"4", xml->AttributeOr(L"fileversion", L"")); // as upstream saves it
        EXPECT_TRUE(Xml(MrWriterStroke)->DeepEquals(*xml->Element(L"page")->Element(L"layer")->Element(L"stroke")));
    }

    // --- Audio ------------------------------------------------------------------------------------------------------

    TEST(FormatCompatibility, AudioAttributesAndElementsSurviveEveryPath)
    {
        TempFolder dir;
        auto path = WriteFile(dir / L"audio.xopp", R"(<?xml version="1.0" standalone="no"?>
<xournal creator="Xournal++ 1.2.3" fileversion="4"><title>t</title><audio fn="rec.mp3"/>
<page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" ts="1500" fn="rec.mp3" color="#000000ff" width="1">0 0 10 10</stroke>
<text font="Sans" size="12" x="5" y="5" color="#000000ff" ts="2500" fn="rec.mp3">Hallo</text>
</layer></page></xournal>)");
        auto doc = Load(path);

        // Unchanged, through a save.
        Save(*doc, path);
        auto xml = ReadXml(path);
        EXPECT_EQ(L"rec.mp3", xml->Element(L"audio")->AttributeOr(L"fn", L""));
        auto layer = xml->Element(L"page")->Element(L"layer");
        EXPECT_EQ(L"1500", layer->Element(L"stroke")->AttributeOr(L"ts", L""));
        EXPECT_EQ(L"2500", layer->Element(L"text")->AttributeOr(L"ts", L""));
        EXPECT_EQ(L"rec.mp3", layer->Element(L"text")->AttributeOr(L"fn", L""));

        // Moved while shown, then in a snapshot.
        auto reloaded = Load(path);
        ShownPage shown(reloaded->Pages()[0]);
        shown.page->Ink().RecordTransform(shown.ids[0], Matrix::Translation(10, 0));
        auto snapshot = TakeSnapshot(*reloaded, path);
        auto stroke = snapshot->Element(L"page")->Element(L"layer")->Element(L"stroke");
        EXPECT_EQ(L"10 0 20 10", stroke->Value());
        EXPECT_EQ(L"1500", stroke->AttributeOr(L"ts", L""));
        EXPECT_EQ(L"rec.mp3", stroke->AttributeOr(L"fn", L""));
        EXPECT_NE(nullptr, snapshot->Element(L"audio"));

        // Copied and pasted.
        auto clip = XoppClip::Create({ 0, 0, 10, 10 }, { shown.page->Ink().ElementOf(shown.ids[0]) });
        auto pasted = XoppClip::Place(*clip, Matrix::Translation(50, 50));
        ASSERT_EQ(1u, pasted.size());
        EXPECT_EQ(L"1500", pasted[0]->AttributeOr(L"ts", L""));
        EXPECT_EQ(L"rec.mp3", pasted[0]->AttributeOr(L"fn", L""));

        // Copied into another document.
        auto copies = CopyPages(*reloaded, { 0 }, [](int p) { return p; });
        auto copiedLayer = copies[0]->Snapshot()->Element(L"layer");
        EXPECT_EQ(L"1500", copiedLayer->Element(L"stroke")->AttributeOr(L"ts", L""));
        EXPECT_EQ(L"2500", copiedLayer->Element(L"text")->AttributeOr(L"ts", L""));
    }

    // --- Layers and backgrounds -------------------------------------------------------------------------------------

    TEST(FormatCompatibility, LayerNamesAreKeptAndNewLayersHaveNone)
    {
        auto doc = XoppDocument::CreateNew();
        doc->Pages().push_back(PageWith(R"(<stroke tool="pen" color="#000000ff" width="1">0 0 1 1</stroke>)", R"( name="Skizze")"));

        // New pages write a layer without name, as upstream.
        EXPECT_EQ(nullptr, doc->Pages()[0]->Snapshot()->Element(L"layer")->Attribute(L"name"));

        // A shown page with a stroke drawn in this session, written back: the name stays.
        {
            ShownPage shown(doc->Pages()[1]);
            auto drawn = shown.ink.Add(InkOfXml(R"(<stroke tool="pen" color="#ff0000ff" width="1">5 5 9 9</stroke>)"));
            shown.ink.SetOrder({ shown.ids[0], drawn });
            EXPECT_EQ(L"Skizze", shown.page->Ink().SnapshotOf(shown.ink)->Element(L"layer")->AttributeOr(L"name", L""));
            shown.page->Ink().Detach();
        }
        EXPECT_EQ(L"Skizze", doc->Pages()[1]->Snapshot()->Element(L"layer")->AttributeOr(L"name", L""));
        EXPECT_EQ(2u, doc->Pages()[1]->Snapshot()->Element(L"layer")->Elements(L"stroke").size());

        // Copying pages keeps it.
        auto copies = CopyPages(*doc, { 1 }, [](int p) { return p; });
        EXPECT_EQ(L"Skizze", copies[0]->Snapshot()->Element(L"layer")->AttributeOr(L"name", L""));
    }

    TEST(FormatCompatibility, APdfBackgroundKeepsItsNameAndOtherAttributesWhenItsReferenceIsWritten)
    {
        TempFolder dir;
        WriteFile(dir / L"lecture.pdf", "%PDF-1.7", false);
        auto path = WriteFile(dir / L"named.xopp", R"(<?xml version="1.0" standalone="no"?>
<xournal creator="Xournal++ 1.2.3" fileversion="4"><title>t</title>
<page width="595" height="842"><background name="Deckblatt" type="pdf" domain="absolute" filename="lecture.pdf" pageno="1" other="x"/><layer/></page>
<page width="595" height="842"><background name="Seite 2" type="pdf" pageno="2"/><layer/></page></xournal>)");
        auto doc = Load(path);
        auto snapshot = TakeSnapshot(*doc, path);
        auto pages = snapshot->Elements(L"page");

        std::vector<std::wstring> names;
        for (auto const& a : pages[0]->Element(L"background")->Attributes())
        {
            names.push_back(a.name);
        }
        EXPECT_EQ((std::vector<std::wstring>{ L"name", L"type", L"domain", L"filename", L"pageno", L"other" }), names);
        EXPECT_EQ(L"Deckblatt", pages[0]->Element(L"background")->AttributeOr(L"name", L""));
        EXPECT_EQ(L"x", pages[0]->Element(L"background")->AttributeOr(L"other", L""));
        EXPECT_EQ(L"Seite 2", pages[1]->Element(L"background")->AttributeOr(L"name", L""));
    }

    TEST(FormatCompatibility, OldXournalPaperColorNamesAreReadAsUpstreamReadsThem)
    {
        auto paper = [](char const* color)
        {
            return XoppPage::FromXml(Xml(std::string(R"(<page width="10" height="10"><background type="solid" color=")") + color +
                R"(" style="plain"/><layer/></page>)"))->BackgroundColor();
        };
        EXPECT_EQ((Color{ 0xFF, 0xa0, 0xe8, 0xff }), paper("blue"));
        EXPECT_EQ((Color{ 0xFF, 0xff, 0xc0, 0xd4 }), paper("pink"));
        EXPECT_EQ((Color{ 0xFF, 0x80, 0xff, 0xc0 }), paper("green"));
        EXPECT_EQ((Color{ 0xFF, 0xff, 0xc0, 0x80 }), paper("orange"));
        EXPECT_EQ((Color{ 0xFF, 0xff, 0xff, 0x80 }), paper("yellow"));
        EXPECT_EQ(Colors::White, paper("white"));        // the pen names otherwise
        EXPECT_EQ((Color{ 0xFF, 0x80, 0x80, 0x80 }), paper("gray"));

        // A pen keeps its own meaning of "blue".
        EXPECT_EQ((Color{ 0xFF, 0x33, 0x33, 0xcc }), Strokes::ReadShape(*Xml(R"(<stroke tool="pen" color="blue" width="1">0 0 1 1</stroke>)"))->color);
    }

    // --- Pictures ---------------------------------------------------------------------------------------------------

    TEST(FormatCompatibility, PictureDataOfAnyFormatIsKeptAsRead)
    {
        // Upstream takes any data gdk-pixbuf reads (here a JPEG) and writes the bytes back as they are.
        auto image = Xml(R"(<image left="10" top="10" right="30" bottom="20">/9j/4AAQSkZJRg==</image>)");
        auto data = XoppImage::GetData(*image);
        ASSERT_GE(data.size(), 2u);
        EXPECT_EQ(0xFF, data[0]);
        EXPECT_EQ(0xD8, data[1]);

        XoppImage::SetBounds(*image, { 0, 0, 40, 20 });
        EXPECT_EQ(L"/9j/4AAQSkZJRg==", image->Value());
        auto pasted = XoppClip::Place(*XoppClip::Create({ 0, 0, 40, 20 }, { image }), Matrix::Translation(5, 5));
        EXPECT_EQ(L"/9j/4AAQSkZJRg==", pasted[0]->Value());
    }
}
