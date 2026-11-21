#include "DarkPdf.h"
#include "DarkColors.h"

#include <gtest/gtest.h>

using namespace xournal::app;

TEST(DarkPdf, ReversesLumaAndKeepsTransparencyAndColor)
{
    CpuBitmap bitmap{ 4, 1, { 255, 255, 255, 255, 0, 0, 0, 128, 0, 0, 0, 0, 40, 80, 140, 255 } };
    DarkPdf::Recolor(bitmap);
    auto const& pixels = bitmap.pixels;
    EXPECT_EQ(pixels[0], DarkColors::Sheet.b);
    EXPECT_EQ(pixels[1], DarkColors::Sheet.g);
    EXPECT_EQ(pixels[2], DarkColors::Sheet.r);
    EXPECT_EQ(pixels[3], 255);
    EXPECT_GT(pixels[4], 100);
    EXPECT_LE(pixels[4], pixels[7]);
    EXPECT_EQ(pixels[7], 128);
    EXPECT_EQ(std::vector<std::uint8_t>(pixels.begin() + 8, pixels.begin() + 12), (std::vector<std::uint8_t>{ 0, 0, 0, 0 }));
    EXPECT_GT(pixels[14], pixels[13]);
    EXPECT_GT(pixels[13], pixels[12]);
}

