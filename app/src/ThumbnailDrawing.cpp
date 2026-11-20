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

    SIZE ThumbnailSize(SIZE box, double aspect)
    {
        if (!(aspect > 0)) aspect = 1.4142;
        LONG width = box.cx, height = std::lround(box.cx * aspect);
        if (height > box.cy)
        {
            height = box.cy;
            width = std::lround(box.cy / aspect);
        }
        return { std::max(1L, width), std::max(1L, height) };
    }

    HIMAGELIST CreateSlotImages(SIZE slot)
    {
        // A mask that is transparent everywhere: an image of alpha 0 alone is drawn black (an image list takes a 32-bit
        // image without any alpha as opaque).
        HIMAGELIST images = ImageList_Create(slot.cx, slot.cy, ILC_COLOR32 | ILC_MASK, 1, 0);
        if (!images) ThrowLastError("ImageList_Create (thumbnail slot)");
        HDC screen = GetDC(nullptr);
        HBITMAP color = CreateCompatibleBitmap(screen, slot.cx, slot.cy);
        ReleaseDC(nullptr, screen);
        HBITMAP mask = CreateBitmap(slot.cx, slot.cy, 1, 1, nullptr);
        bool made = color && mask;
        if (made)
        {
            HDC dc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(dc, mask);
            made = PatBlt(dc, 0, 0, slot.cx, slot.cy, WHITENESS) != FALSE; // 1 = transparent
            SelectObject(dc, old);
            DeleteDC(dc);
        }
        int added = made ? ImageList_Add(images, color, mask) : -1;
        DWORD error = GetLastError();
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        if (added < 0)
        {
            ImageList_Destroy(images);
            SetLastError(error);
            ThrowLastError("thumbnail slot image");
        }
        return images;
    }

    void DrawPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect, CpuBitmap const* background, CpuBitmap const* content)
    {
        SIZE size = ThumbnailSize(box, aspect);
        RECT page{ area.left + (area.right - area.left - size.cx) / 2, area.top + (area.bottom - area.top - size.cy) / 2, 0, 0 };
        page.right = page.left + size.cx;
        page.bottom = page.top + size.cy;
        FillRect(dc, &page, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        if (background) Blend(dc, *background, page);
        if (content) Blend(dc, *content, page);
        RECT frame = page;
        InflateRect(&frame, 1, 1); // around the page, not over its edge
        HBRUSH border = CreateSolidBrush(RGB(0xC8, 0xC8, 0xC8));
        FrameRect(dc, &frame, border);
        DeleteObject(border);
    }
}
