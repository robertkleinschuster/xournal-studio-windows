// Places of the background PDF in the document and back (outline entries, links, search hits).

#include "xournal/model/XoppDocument.h"
#include "xournal/model/XoppPage.h"
#include "xournal/pdf/PdfPlaces.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using namespace xournal::pdf;

    TEST(PdfPlaces, ADestinationGoesToThePageShowingItsPdfPageNearestToTheView)
    {
        auto doc = XoppDocument::FromPdf(L"C:\\x\\a.pdf", { { 600, 800 }, { 600, 800 } });
        doc->Pages().push_back(XoppPage::CreatePdf(600, 800, 0)); // PDF page 1 shown a second time, as page 3
        auto unknown = [](int) { return std::optional<Size>(); };

        auto place = FindPlace(*doc, { 0, Point{ 100, 200 } }, 2, unknown);
        ASSERT_TRUE(place);
        EXPECT_EQ(2, place->page); // nearer to the view than page 1
        EXPECT_EQ((Point{ 100, 200 }), place->point);
        EXPECT_EQ(0, FindPlace(*doc, { 0, std::nullopt }, 0, unknown)->page);
        EXPECT_EQ((Point{ 0, 0 }), FindPlace(*doc, { 1, std::nullopt }, 0, unknown)->point); // no position: the top left
        EXPECT_FALSE(FindPlace(*doc, { 5, std::nullopt }, 0, unknown)); // no page shows PDF page 6
    }

    TEST(PdfPlaces, APdfPageStretchedOverItsPageScalesItsPlaces)
    {
        auto doc = XoppDocument::FromPdf(L"C:\\x\\a.pdf", { { 600, 800 } });
        auto half = [](int) { return std::optional<Size>(Size{ 300, 400 }); }; // the page shows it at twice its size
        EXPECT_EQ((Point{ 200, 400 }), FindPlace(*doc, { 0, Point{ 100, 200 } }, 0, half)->point);
        EXPECT_EQ((Point{ 600, 800 }), FindPlace(*doc, { 0, Point{ 1000, 1000 } }, 0, half)->point); // kept on the page
        auto const& page = *doc->Pages()[0];
        EXPECT_EQ((Point{ 50, 100 }), *ToShown(page, { 100, 200 }, half));
        EXPECT_EQ((Rect{ 20, 40, 60, 80 }), FromShown(page, { 10, 20, 30, 40 }, half));
        EXPECT_FALSE(ToShown(*XoppDocument::CreateNew()->Pages()[0], { 0, 0 }, half)); // a graph page shows no PDF
    }
}
