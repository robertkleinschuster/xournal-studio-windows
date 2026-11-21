#pragma once

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "inkpages/model/PaperAppearance.h"
#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/model/Color.h"

namespace xournal::app::DarkColors
{
    inline constexpr COLORREF Background = RGB(32, 33, 36);
    inline constexpr COLORREF Text = RGB(238, 238, 240);
    inline constexpr COLORREF Selected = RGB(59, 75, 101);
    inline constexpr COLORREF Input = RGB(48, 51, 57);
    inline constexpr wchar_t WindowProperty[] = L"XournalStudio.DarkMode";

    inline bool Enabled(HWND window)
    {
        return GetPropW(GetAncestor(window, GA_ROOTOWNER), WindowProperty) != nullptr;
    }

    inline void SetCheckBoxPalette(HWND control, bool dark)
    {
        wchar_t name[16]{};
        if (!GetClassNameW(control, name, ARRAYSIZE(name)) || wcscmp(name, WC_BUTTONW) != 0) return;
        auto style = GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK;
        if (style != BS_AUTOCHECKBOX && style != BS_CHECKBOX) return;
        // Let the checkbox honor its parent's WM_CTLCOLORBTN palette.
        HRESULT hr = SetWindowTheme(control, dark ? L"" : nullptr, dark ? L"" : nullptr);
        if (FAILED(hr)) LogError("SetWindowTheme (checkbox): " + DescribeHr(hr));
    }

    inline void SetTooltipPalette(HWND tooltip, bool dark)
    {
        // The tooltip color messages only apply when visual styles are disabled for this window.
        Check("SetWindowTheme (tooltip)", SetWindowTheme(tooltip, dark ? L"" : nullptr, dark ? L"" : nullptr));
        SendMessageW(tooltip, TTM_SETTIPBKCOLOR, dark ? Input : GetSysColor(COLOR_INFOBK), 0);
        SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, dark ? Text : GetSysColor(COLOR_INFOTEXT), 0);
    }

    inline HBRUSH ControlBrush(HDC dc, COLORREF background)
    {
        SetTextColor(dc, Text);
        SetBkColor(dc, background);
        SetDCBrushColor(dc, background);
        return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    }

    inline bool PaintBackground(HWND window, HDC dc)
    {
        RECT rect{};
        return GetClientRect(window, &rect) && FillRect(dc, &rect, ControlBrush(dc, Background));
    }

    inline constexpr Color Sheet{ inkpages::PaperAppearance::DarkSheet.a, inkpages::PaperAppearance::DarkSheet.r,
        inkpages::PaperAppearance::DarkSheet.g, inkpages::PaperAppearance::DarkSheet.b };

    inline Color Ink(Color original, std::uint8_t opacity, bool highlighter = false)
    {
        auto visible = inkpages::PaperAppearance::DarkInk({ opacity, original.r, original.g, original.b }, highlighter);
        return { original.a, visible.r, visible.g, visible.b };
    }
}
