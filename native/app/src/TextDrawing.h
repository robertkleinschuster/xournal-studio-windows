#pragma once

// Texts of a page (<text>, <link>) drawn with DirectWrite as upstream lays them out: the box's top left at x,y (or placed
// by the matrix), the Pango font mapped to a Windows family, size in points, color, wrap width, alignment, justify.

#include "xournal/model/XoppText.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace xournal::app
{
    class TextDrawing
    {
    public:
        TextDrawing();

        /// Draws the text (the dc's transform maps page units; it is restored afterwards).
        void Draw(ID2D1DeviceContext* dc, XoppText const& text);

    private:
        Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    };
}
