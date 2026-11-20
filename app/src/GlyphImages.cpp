#include "GlyphImages.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <algorithm>
#include <cstdint>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t FontName[] = L"Segoe Fluent Icons";

        // SelectObject documents no last error: a failure is E_FAIL with the step's name.
        HGDIOBJ Select(HDC dc, HGDIOBJ object, char const* what)
        {
            HGDIOBJ old = SelectObject(dc, object);
            if (!old || old == HGDI_ERROR) ThrowHr(std::format("SelectObject ({})", what), E_FAIL);
            return old;
        }

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
            HGDIOBJ oldBitmap = nullptr, oldFont = nullptr;
            try
            {
                oldBitmap = Select(dc, bitmap, "glyph bitmap");
                oldFont = Select(dc, font, "glyph font");
                if (!SetBkMode(dc, TRANSPARENT)) ThrowHr("SetBkMode (glyph)", E_FAIL);
                if (SetTextColor(dc, RGB(255, 255, 255)) == CLR_INVALID) ThrowHr("SetTextColor (glyph)", E_FAIL);
                RECT r{ 0, 0, size, size };
                if (!DrawTextW(dc, &glyph, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX))
                {
                    ThrowLastError(std::format("DrawText (glyph U+{:04X})", static_cast<unsigned>(glyph)));
                }
                if (!GdiFlush()) ThrowHr("GdiFlush (glyph)", E_FAIL);
                Select(dc, oldFont, "previous font");
                oldFont = nullptr;
                Select(dc, oldBitmap, "previous bitmap");
                oldBitmap = nullptr;
            }
            catch (...)
            {
                if (oldFont) SelectObject(dc, oldFont);
                if (oldBitmap) SelectObject(dc, oldBitmap);
                DeleteObject(bitmap);
                throw;
            }
            for (int i = 0; i < size * size; ++i)
            {
                std::uint32_t a = (bits[i] >> 16) & 0xFF;
                bits[i] = (a << 24) | ((GetRValue(ink) * a / 255) << 16) | ((GetGValue(ink) * a / 255) << 8) | (GetBValue(ink) * a / 255);
            }
            return bitmap;
        }

        // GDI substitutes another font when the asked one is missing: the glyphs would be wrong (logged, not fatal).
        void CheckFace(HDC dc, HFONT font)
        {
            HGDIOBJ old = Select(dc, font, "font check");
            wchar_t face[LF_FACESIZE]{};
            int length = GetTextFaceW(dc, LF_FACESIZE, face);
            SelectObject(dc, old);
            if (length == 0) ThrowHr("GetTextFace (glyph font)", E_FAIL);
            if (std::wstring_view(face) != FontName)
            {
                LogError(std::format("glyph images: the font is '{}', not Segoe Fluent Icons (the icons are wrong)", ToUtf8(face)));
            }
        }
    }

    HIMAGELIST CreateGlyphImages(std::initializer_list<wchar_t> glyphs, int size)
    {
        HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32, static_cast<int>(glyphs.size()), 0);
        if (!images) ThrowLastError("ImageList_Create (glyphs)");
        HDC dc = nullptr;
        HFONT font = nullptr;
        try
        {
            dc = CreateCompatibleDC(nullptr);
            if (!dc) ThrowLastError("CreateCompatibleDC (glyphs)");
            font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, FontName);
            if (!font) ThrowLastError("CreateFont (Segoe Fluent Icons)");
            CheckFace(dc, font);
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
