#pragma once

#include "Bitmaps.h"

namespace xournal::app::DarkPdf
{
    // Display-only luma reversal, with hue and alpha retained where the output gamut permits.
    void Recolor(CpuBitmap& bitmap);

    class Drawing
    {
    public:
        void Draw(ID2D1DeviceContext* dc, ID2D1Bitmap* bitmap, D2D1_RECT_F dest,
            D2D1_INTERPOLATION_MODE interpolation);

    private:
        Microsoft::WRL::ComPtr<ID2D1Device> device;
        Microsoft::WRL::ComPtr<ID2D1Effect> matrix;
    };
}
