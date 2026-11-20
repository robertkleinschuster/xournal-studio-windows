#include "xournal/pdf/RenderTiles.h"

#include <algorithm>
#include <cstdlib>

namespace xournal::pdf
{
    namespace
    {
        int CeilDiv(int a, int b) { return (a + b - 1) / b; }
    }

    TileGrid::TileGrid(int pageWidth, int pageHeight, int tile)
        : width(std::max(0, pageWidth)), height(std::max(0, pageHeight)), tile(std::max(1, tile)),
          columns(CeilDiv(width, this->tile)), rows(CeilDiv(height, this->tile))
    {
    }

    PixelRect TileGrid::Rect(int index) const
    {
        if (index < 0 || index >= Count()) return {};
        int column = index % columns, row = index / columns;
        return { column * tile, row * tile, std::min(width, (column + 1) * tile), std::min(height, (row + 1) * tile) };
    }

    std::vector<int> TileGrid::Covering(PixelRect area) const
    {
        std::vector<int> tiles;
        area = { std::max(0, area.left), std::max(0, area.top), std::min(width, area.right), std::min(height, area.bottom) };
        if (area.Empty()) return tiles;
        int firstColumn = area.left / tile, lastColumn = (area.right - 1) / tile;
        int firstRow = area.top / tile, lastRow = (area.bottom - 1) / tile;
        for (int row = firstRow; row <= lastRow; ++row)
            for (int column = firstColumn; column <= lastColumn; ++column) tiles.push_back(row * columns + column);
        // Twice the distance between centers (integers): the tile in the middle of the area first.
        auto distance = [&](int index)
        {
            PixelRect r = Rect(index);
            long long dx = (r.left + r.right) - (area.left + area.right), dy = (r.top + r.bottom) - (area.top + area.bottom);
            return dx * dx + dy * dy;
        };
        std::stable_sort(tiles.begin(), tiles.end(), [&](int a, int b) { return distance(a) < distance(b); });
        return tiles;
    }
}
