#pragma once

// The tiles a PDF page is rendered in on screen: a fixed grid of square tiles over the page's pixels at one zoom (pixel =
// page unit x pixels per unit), so each tile is rendered once per zoom and kept while the view scrolls over it. Pure
// geometry (no Windows), unit-tested.

#include <vector>

namespace xournal::pdf
{
    struct PixelRect
    {
        int left = 0, top = 0, right = 0, bottom = 0;

        int Width() const { return right - left; }
        int Height() const { return bottom - top; }
        bool Empty() const { return right <= left || bottom <= top; }
        bool operator==(PixelRect const&) const = default;
    };

    class TileGrid
    {
    public:
        /// A page of `pageWidth` x `pageHeight` pixels in tiles of `tile` x `tile` pixels (the last row and column
        /// clipped to the page).
        TileGrid(int pageWidth, int pageHeight, int tile);

        int Columns() const { return columns; }
        int Rows() const { return rows; }
        int Count() const { return columns * rows; }

        /// Tile `index` (row * Columns() + column) in page pixels, clipped to the page; empty for an index not in the grid.
        PixelRect Rect(int index) const;

        /// The tiles `area` (page pixels) touches, the one nearest to the area's center first (it is in view first).
        std::vector<int> Covering(PixelRect area) const;

    private:
        int width, height, tile, columns, rows;
    };
}
