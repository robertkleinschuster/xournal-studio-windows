// The PDF's navigation (outline and links) read with qpdf from memory; synthetic PDFs built with qpdf.

#include "PdfTestSupport.h"
#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/pdf/PageFrame.h"
#include "xournal/pdf/PdfNavigation.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFWriter.hh>

#include <gtest/gtest.h>

namespace xournal::tests
{
    namespace
    {
        using namespace xournal::pdf;

        QPDFObjectHandle Name(char const* n) { return QPDFObjectHandle::newName(n); }
        QPDFObjectHandle Num(double v) { return QPDFObjectHandle::newReal(v, 3); }

        QPDFObjectHandle Array(std::vector<QPDFObjectHandle> items)
        {
            auto a = QPDFObjectHandle::newArray();
            for (auto& i : items) a.appendItem(i);
            return a;
        }

        struct NavigationPdf
        {
            QPDF pdf;
            std::vector<QPDFObjectHandle> pages;

            NavigationPdf()
            {
                pdf.emptyPDF();
                for (int i = 0; i < 3; ++i)
                {
                    auto page = QPDFObjectHandle::newDictionary();
                    page.replaceKey("/Type", Name("/Page"));
                    page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, 595, 842)));
                    page.replaceKey("/Contents", pdf.newStream(std::string()));
                    if (i == 1)
                    {
                        page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
                        page.replaceKey("/CropBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(36, 36, 559, 806)));
                    }
                    pdf.addPage(pdf.makeIndirectObject(page), false);
                }
                pages = pdf.getAllPages();
            }

            QPDFObjectHandle Item(std::string const& title, QPDFObjectHandle parent)
            {
                auto item = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
                item.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(title));
                item.replaceKey("/Parent", parent);
                return item;
            }

            static void Chain(QPDFObjectHandle parent, std::vector<QPDFObjectHandle> kids)
            {
                parent.replaceKey("/First", kids.front());
                parent.replaceKey("/Last", kids.back());
                parent.replaceKey("/Count", QPDFObjectHandle::newInteger(static_cast<long long>(kids.size())));
                for (size_t i = 0; i + 1 < kids.size(); ++i)
                {
                    kids[i].replaceKey("/Next", kids[i + 1]);
                    kids[i + 1].replaceKey("/Prev", kids[i]);
                }
            }

            std::vector<std::uint8_t> Bytes()
            {
                QPDFWriter writer(pdf);
                writer.setOutputMemory();
                writer.write();
                std::unique_ptr<Buffer> buffer(writer.getBuffer());
                return { buffer->getBuffer(), buffer->getBuffer() + buffer->getSize() };
            }
        };

        QPDFObjectHandle Link(QPDF& pdf, QPDFObjectHandle::Rectangle rect)
        {
            auto a = QPDFObjectHandle::newDictionary();
            a.replaceKey("/Type", Name("/Annot"));
            a.replaceKey("/Subtype", Name("/Link"));
            a.replaceKey("/Rect", QPDFObjectHandle::newArray(rect));
            return pdf.makeIndirectObject(a);
        }

        QPDFObjectHandle Action(char const* kind, char const* key, QPDFObjectHandle value)
        {
            auto a = QPDFObjectHandle::newDictionary();
            a.replaceKey("/S", Name(kind));
            a.replaceKey(key, value);
            return a;
        }
    }

    TEST(PdfPageFrame, RotationAndCropBox)
    {
        auto frame = PageFrame::Of(PdfBox{ 36, 36, 559, 806 }, PdfBox{ 0, 0, 595, 842 }, -270); // -270 = 90
        EXPECT_EQ(frame.rotate, 90);
        EXPECT_EQ(frame.Shown(), (Size{ 770, 523 }));
        EXPECT_EQ(frame.FromUserSpace(36, 806), (Point{ 770, 0 })); // the box's top left goes to the top right
        auto plain = PageFrame::Of(std::nullopt, PdfBox{ 0, 0, 595, 842 }, 0);
        EXPECT_EQ(plain.Area(PdfBox{ 100, 742, 200, 792 }), (Rect{ 100, 50, 100, 50 }));
    }

    TEST(PdfNavigation, NestedOutlineWithNamedDestinations)
    {
        NavigationPdf doc;
        auto& pdf = doc.pdf;
        // /Dests (names) and the /Names tree (strings).
        auto dests = QPDFObjectHandle::newDictionary();
        dests.replaceKey("/sec11", Array({ doc.pages[1], Name("/FitH"), Num(500) }));
        pdf.getRoot().replaceKey("/Dests", pdf.makeIndirectObject(dests));
        auto tree = QPDFObjectHandle::newDictionary();
        tree.replaceKey("/Names", Array({ QPDFObjectHandle::newString("sec12"), Array({ doc.pages[2], Name("/Fit") }) }));
        auto names = QPDFObjectHandle::newDictionary();
        names.replaceKey("/Dests", pdf.makeIndirectObject(tree));
        pdf.getRoot().replaceKey("/Names", names);

        auto outlines = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        outlines.replaceKey("/Type", Name("/Outlines"));
        auto chapter = doc.Item("Kapitel 1 – Grundlagen", outlines);
        chapter.replaceKey("/Dest", Array({ doc.pages[0], Name("/XYZ"), Num(72), Num(700), Num(0) }));
        auto s11 = doc.Item("1.1", chapter);
        s11.replaceKey("/Dest", Name("/sec11"));
        auto s12 = doc.Item("1.2", chapter);
        s12.replaceKey("/A", Action("/GoTo", "/D", QPDFObjectHandle::newString("sec12")));
        NavigationPdf::Chain(chapter, { s11, s12 });
        auto group = doc.Item("Gruppe", outlines);
        auto broken = doc.Item("Kaputt", group);
        broken.replaceKey("/Dest", Name("/missing"));
        NavigationPdf::Chain(group, { broken });
        auto brokenLeaf = doc.Item("Ins Leere", outlines);
        brokenLeaf.replaceKey("/Dest", QPDFObjectHandle::newString("nowhere"));
        NavigationPdf::Chain(outlines, { chapter, group, brokenLeaf });
        pdf.getRoot().replaceKey("/Outlines", outlines);

        PdfNavigation navigation(doc.Bytes());
        auto outline = navigation.Outline();

        ASSERT_EQ(outline.size(), 2u); // the broken leaf is left out
        EXPECT_EQ(outline[0].title, L"Kapitel 1 – Grundlagen");
        ASSERT_TRUE(outline[0].destination);
        EXPECT_EQ(outline[0].destination->page, 0);
        EXPECT_EQ(outline[0].destination->position, (Point{ 72, 842 - 700 }));
        ASSERT_EQ(outline[0].children.size(), 2u);
        auto const& named = *outline[0].children[0].destination;
        EXPECT_EQ(named.page, 1);
        EXPECT_EQ(named.position, (Point{ 770 - 306, 0 })); // FitH 500 on the turned, cropped page
        auto const& stringNamed = *outline[0].children[1].destination;
        EXPECT_EQ(stringNamed.page, 2);
        EXPECT_FALSE(stringNamed.position); // /Fit names no position
        EXPECT_EQ(outline[1].title, L"Gruppe");
        EXPECT_FALSE(outline[1].destination);
        EXPECT_TRUE(outline[1].children.empty()); // its broken entry is left out
    }

    TEST(PdfNavigation, LinksOnARotatedPage)
    {
        NavigationPdf doc;
        auto& pdf = doc.pdf;
        auto uri = Link(pdf, { 100, 200, 200, 220 });
        uri.replaceKey("/A", Action("/URI", "/URI", QPDFObjectHandle::newString("https://example.org/skript")));
        auto goTo = Link(pdf, { 300, 400, 350, 450 });
        goTo.replaceKey("/Dest", Array({ doc.pages[0], Name("/XYZ"), QPDFObjectHandle::newNull(), Num(400), QPDFObjectHandle::newNull() }));
        auto action = Link(pdf, { 10, 10, 20, 20 });
        action.replaceKey("/A", Action("/GoTo", "/D", Array({ doc.pages[2], Name("/FitR"), Num(10), Num(20), Num(30), Num(40) })));
        auto hidden = Link(pdf, { 400, 400, 450, 450 });
        hidden.replaceKey("/F", QPDFObjectHandle::newInteger(2));
        hidden.replaceKey("/A", Action("/URI", "/URI", QPDFObjectHandle::newString("https://hidden.example/")));
        auto script = Link(pdf, { 450, 450, 500, 500 });
        script.replaceKey("/A", Action("/JavaScript", "/JS", QPDFObjectHandle::newString("app.alert(1)")));
        doc.pages[1].replaceKey("/Annots", Array({ uri, goTo, action, hidden, script }));

        PdfNavigation navigation(doc.Bytes());
        EXPECT_EQ(navigation.PageCount(), 3);
        EXPECT_TRUE(navigation.Links(0).empty());
        auto links = navigation.Links(1);

        ASSERT_EQ(links.size(), 3u); // hidden and JavaScript left out
        // Shown coordinates of the page turned by 90 degrees with its crop box: (x, y) -> (y - 36, x - 36).
        EXPECT_EQ(links[0].area, (Rect{ 164, 64, 20, 100 }));
        EXPECT_EQ(std::get<std::wstring>(links[0].target), L"https://example.org/skript");
        auto const& internal = std::get<Destination>(links[1].target);
        EXPECT_EQ(internal.page, 0);
        EXPECT_EQ(internal.position, (Point{ 0, 842 - 400 })); // left open: the page's left edge
        auto const& fitR = std::get<Destination>(links[2].target);
        EXPECT_EQ(fitR.page, 2);
        EXPECT_EQ(fitR.position, (Point{ 10, 842 - 40 })); // FitR: left and top of the rectangle
        EXPECT_THROW(navigation.Links(3), Error);
    }

    TEST(PdfNavigation, NoOutlineAndDamagedData)
    {
        NavigationPdf doc;
        PdfNavigation navigation(doc.Bytes());
        EXPECT_TRUE(navigation.Outline().empty());
        std::vector<std::uint8_t> junk{ '%', 'P', 'D', 'F', '-', '1', '.', '7', '\n', 'x' };
        EXPECT_THROW(PdfNavigation{ junk }, Error);
    }
}
