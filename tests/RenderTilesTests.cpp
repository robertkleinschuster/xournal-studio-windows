// The tile grid PDF pages are rendered in on screen.

#include "xournal/pdf/RenderTiles.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace xournal::tests
{
    using namespace xournal::pdf;

    TEST(RenderTiles, TheLastRowAndColumnAreClippedToThePage)
    {
        TileGrid grid(1000, 1500, 512);
        EXPECT_EQ(grid.Columns(), 2);
        EXPECT_EQ(grid.Rows(), 3);
        EXPECT_EQ(grid.Rect(0), (PixelRect{ 0, 0, 512, 512 }));
        EXPECT_EQ(grid.Rect(1), (PixelRect{ 512, 0, 1000, 512 }));
        EXPECT_EQ(grid.Rect(5), (PixelRect{ 512, 1024, 1000, 1500 }));
        EXPECT_TRUE(grid.Rect(6).Empty());
        EXPECT_TRUE(grid.Rect(-1).Empty());
    }

    TEST(RenderTiles, AnExactMultipleHasNoEmptyTiles)
    {
        TileGrid grid(1024, 512, 512);
        EXPECT_EQ(grid.Count(), 2);
        EXPECT_EQ(grid.Covering({ 0, 0, 1024, 512 }).size(), 2u);
    }

    TEST(RenderTiles, CoveringListsTheTouchedTilesCenterFirst)
    {
        TileGrid grid(3000, 3000, 1000); // 3 x 3
        auto tiles = grid.Covering({ 900, 900, 2100, 2100 });
        ASSERT_EQ(tiles.size(), 9u);
        EXPECT_EQ(tiles.front(), 4); // the middle tile
        auto sorted = tiles;
        std::sort(sorted.begin(), sorted.end());
        for (int i = 0; i < 9; ++i) EXPECT_EQ(sorted[i], i);
    }

    TEST(RenderTiles, CoveringStaysOnThePage)
    {
        TileGrid grid(1000, 1000, 512);
        EXPECT_EQ(grid.Covering({ -500, -500, 10, 10 }), std::vector<int>{ 0 });
        EXPECT_EQ(grid.Covering({ 990, 990, 5000, 5000 }), std::vector<int>{ 3 });
        EXPECT_TRUE(grid.Covering({ 1000, 0, 2000, 100 }).empty());
        EXPECT_TRUE(grid.Covering({ 100, 100, 100, 200 }).empty());
    }

    TEST(RenderTiles, AnAreaOnATileBorderTouchesOnlyTheTilesItCovers)
    {
        TileGrid grid(2048, 2048, 512);
        auto tiles = grid.Covering({ 512, 512, 1024, 1024 });
        EXPECT_EQ(tiles, std::vector<int>{ 5 });
    }
}
