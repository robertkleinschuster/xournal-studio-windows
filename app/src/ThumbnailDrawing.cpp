#include "ThumbnailDrawing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace xournal::app
{
    namespace
    {
        // A memory bitmap (premultiplied BGRA) alpha-blended into `area`.
        void Blend(HDC dc, CpuBitmap const& bitmap, RECT const& area)
        {
            BITMAPINFO info{};
            info.bmiHeader = { sizeof(BITMAPINFOHEADER), static_cast<LONG>(bitmap.width), -static_cast<LONG>(bitmap.height), 1, 32, BI_RGB };
            void* bits = nullptr;
            HBITMAP section = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!section)
            {
                LogError(std::format("thumbnail: CreateDIBSection: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                return;
            }
            std::memcpy(bits, bitmap.pixels.data(), bitmap.pixels.size());
            HDC memory = CreateCompatibleDC(dc);
            HGDIOBJ old = SelectObject(memory, section);
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            SetStretchBltMode(dc, HALFTONE);
            if (!AlphaBlend(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, memory, 0, 0,
                    static_cast<int>(bitmap.width), static_cast<int>(bitmap.height), blend))
            {
                LogError("thumbnail: AlphaBlend failed");
            }
            SelectObject(memory, old);
            DeleteDC(memory);
            DeleteObject(section);
        }
    }

    void DrawPageThumbnail(HDC dc, RECT const& area, double aspect, CpuBitmap const* background, CpuBitmap const* content)
    {
        int width = area.right - area.left, height = area.bottom - area.top;
        int pageHeight = std::min(height, static_cast<int>(std::lround(width * aspect)));
        int pageWidth = std::min(width, static_cast<int>(std::lround(pageHeight / aspect)));
        RECT page{ area.left + (width - pageWidth) / 2, area.top + (height - pageHeight) / 2, 0, 0 };
        page.right = page.left + pageWidth;
        page.bottom = page.top + pageHeight;
        FillRect(dc, &page, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        if (background) Blend(dc, *background, page);
        if (content) Blend(dc, *content, page);
        HBRUSH border = CreateSolidBrush(RGB(0xC8, 0xC8, 0xC8));
        FrameRect(dc, &page, border);
        DeleteObject(border);
    }
}
