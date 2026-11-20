#include "GlyphImages.h"

#include "xournal/base/Error.h"

#include <algorithm>
#include <cstdint>

namespace xournal::app
{
    namespace
    {
        // The glyph white on transparent black, so its red channel is the coverage; then colored and premultiplied.
        HBITMAP GlyphBitmap(HDC dc, HFONT font, wchar_t glyph, int size, COLORREF ink)
        {
            BITMAPINFO bi{};
            bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
            void* raw = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
            if (!bitmap) ThrowLastError("CreateDIBSection (glyph)");
            auto* bits = static_cast<std::uint32_t*>(raw);
            std::fill(bits, bits + size * size, 0u);
            HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
            HGDIOBJ oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT r{ 0, 0, size, size };
            DrawTextW(dc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            GdiFlush();
            SelectObject(dc, oldFont);
            SelectObject(dc, oldBitmap);
            for (int i = 0; i < size * size; ++i)
            {
                std::uint32_t a = (bits[i] >> 16) & 0xFF;
                bits[i] = (a << 24) | ((GetRValue(ink) * a / 255) << 16) | ((GetGValue(ink) * a / 255) << 8) | (GetBValue(ink) * a / 255);
            }
            return bitmap;
        }
    }

    HIMAGELIST CreateGlyphImages(std::initializer_list<wchar_t> glyphs, int size)
    {
        HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32, static_cast<int>(glyphs.size()), 0);
        if (!images) ThrowLastError("ImageList_Create (glyphs)");
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0,
            L"Segoe Fluent Icons");
        try
        {
            if (!dc || !font) ThrowLastError("glyph font");
            for (wchar_t glyph : glyphs)
            {
                HBITMAP bitmap = GlyphBitmap(dc, font, glyph, size, GetSysColor(COLOR_BTNTEXT));
                int added = ImageList_Add(images, bitmap, nullptr);
                DeleteObject(bitmap);
                if (added < 0) ThrowLastError("ImageList_Add (glyph)");
            }
        }
        catch (...)
        {
            if (font) DeleteObject(font);
            if (dc) DeleteDC(dc);
            ImageList_Destroy(images);
            throw;
        }
        DeleteObject(font);
        DeleteDC(dc);
        return images;
    }
}
