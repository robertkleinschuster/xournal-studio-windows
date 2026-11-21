#include "ThumbnailDrawing.h"
#include "DarkColors.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <vector>

namespace xournal::app
{
    ThumbnailBitmap::ThumbnailBitmap(CpuBitmap const& pixels)
        : size{ static_cast<LONG>(pixels.width), static_cast<LONG>(pixels.height) }
    {
        BITMAPINFO info{};
        info.bmiHeader = { sizeof(BITMAPINFOHEADER), size.cx, -size.cy, 1, 32, BI_RGB };
        dc = CreateCompatibleDC(nullptr);
        if (!dc) ThrowLastError("CreateCompatibleDC (thumbnail)");
        try
        {
            void* bits = nullptr;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap) ThrowLastError("CreateDIBSection (thumbnail)");
            HGDIOBJ previous = SelectObject(dc, bitmap);
            if (!previous || previous == HGDI_ERROR) ThrowLastError("SelectObject (thumbnail)");
            std::memcpy(bits, pixels.pixels.data(), pixels.pixels.size());
        }
        catch (...)
        {
            DeleteDC(dc);
            if (bitmap) DeleteObject(bitmap);
            throw;
        }
    }

    ThumbnailBitmap::~ThumbnailBitmap()
    {
        // Deleting the memory DC deselects the DIB before it is deleted.
        if (!DeleteDC(dc)) LogError("thumbnail: DeleteDC failed");
        if (!DeleteObject(bitmap)) LogError("thumbnail: DeleteObject failed");
    }

    void ThumbnailBitmap::Draw(HDC target, RECT const& area) const
    {
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        if (!AlphaBlend(target, area.left, area.top, area.right - area.left, area.bottom - area.top,
            dc, 0, 0, size.cx, size.cy, blend))
        {
            LogError(std::format("thumbnail: AlphaBlend: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
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
        if (slot.cx <= 0 || slot.cy <= 0) ThrowHr("thumbnail slot dimensions", E_INVALIDARG);
        // Each CreateBitmap scan line is word aligned. Set only the pixel bits; padding stays zero.
        auto width = static_cast<size_t>(slot.cx);
        auto height = static_cast<size_t>(slot.cy);
        size_t maskStride = ((width + 15) / 16) * 2;
        std::vector<BYTE> maskBits(maskStride * height, 0);
        for (size_t y = 0; y < height; ++y)
        {
            BYTE* row = maskBits.data() + y * maskStride;
            std::fill_n(row, width / 8, BYTE{ 0xFF });
            if (width % 8) row[width / 8] = static_cast<BYTE>(0xFFu << (8 - width % 8));
        }
        struct SlotBitmaps
        {
            HBITMAP mask{}, color{};
            ~SlotBitmaps()
            {
                if (mask && !DeleteObject(mask)) LogError("thumbnail slot: DeleteObject (mask) failed");
                if (color && !DeleteObject(color)) LogError("thumbnail slot: DeleteObject (color) failed");
            }
        } bitmaps;
        bitmaps.mask = CreateBitmap(slot.cx, slot.cy, 1, 1, maskBits.data());
        if (!bitmaps.mask) ThrowLastError("CreateBitmap (thumbnail slot mask)");

        BITMAPINFO info{};
        info.bmiHeader = { sizeof(BITMAPINFOHEADER), slot.cx, -slot.cy, 1, 32, BI_RGB };
        void* pixels = nullptr;
        bitmaps.color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmaps.color) ThrowLastError("CreateDIBSection (thumbnail slot color)");
        std::memset(pixels, 0, width * height * 4);

        HIMAGELIST images = ImageList_Create(slot.cx, slot.cy, ILC_COLOR32 | ILC_MASK, 1, 0);
        if (!images) ThrowHr("ImageList_Create (thumbnail slot)", E_FAIL);
        if (ImageList_Add(images, bitmaps.color, bitmaps.mask) < 0)
        {
            if (!ImageList_Destroy(images)) LogError("thumbnail slot: ImageList_Destroy failed");
            ThrowHr("ImageList_Add (thumbnail slot)", E_FAIL);
        }
        return images;
    }

    void DrawCachedPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect,
        ThumbnailBitmap const* background, ThumbnailBitmap const* content, bool darkPaper)
    {
        SIZE size = ThumbnailSize(box, aspect);
        RECT page{ area.left + (area.right - area.left - size.cx) / 2, area.top + (area.bottom - area.top - size.cy) / 2, 0, 0 };
        page.right = page.left + size.cx;
        page.bottom = page.top + size.cy;
        auto brush = static_cast<HBRUSH>(GetStockObject(darkPaper ? DC_BRUSH : WHITE_BRUSH));
        if (!brush) ThrowHr("GetStockObject (thumbnail paper)", E_FAIL);
        COLORREF previous = darkPaper ? SetDCBrushColor(dc, RGB(DarkColors::Sheet.r, DarkColors::Sheet.g, DarkColors::Sheet.b)) : 0;
        if (previous == CLR_INVALID) ThrowLastError("SetDCBrushColor (thumbnail paper)");
        int filled = FillRect(dc, &page, brush);
        if (darkPaper && SetDCBrushColor(dc, previous) == CLR_INVALID) ThrowLastError("restore DC brush (thumbnail paper)");
        if (!filled) ThrowHr("FillRect (thumbnail paper)", E_FAIL);
        if (background) background->Draw(dc, page);
        if (content) content->Draw(dc, page);
        RECT frame = page;
        InflateRect(&frame, 1, 1); // around the page, not over its edge
        HBRUSH border = CreateSolidBrush(RGB(0xC8, 0xC8, 0xC8));
        FrameRect(dc, &frame, border);
        DeleteObject(border);
    }

    void DrawPageThumbnail(HDC dc, RECT const& area, SIZE box, double aspect, CpuBitmap const* background)
    {
        std::optional<ThumbnailBitmap> layer;
        if (background) layer.emplace(*background);
        DrawCachedPageThumbnail(dc, area, box, aspect, layer ? &*layer : nullptr, nullptr);
    }
}
