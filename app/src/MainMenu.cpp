#include "MainMenu.h"

#include "Commands.h"
#include "DarkColors.h"
#include "Resources.h"
#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <shlwapi.h>
#include <commctrl.h>

#include <format>
#include <cwctype>
#include <string>

namespace xournal::app
{
    namespace
    {
        void DestroyOwnedMenu(HMENU menu)
        {
            if (!DestroyMenu(menu))
                LogError("DestroyMenu (main menu): " + DescribeHr(HRESULT_FROM_WIN32(GetLastError())));
        }
    }

    MainMenu::MainMenu()
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDA_MAIN));
        if (!accelerators) ThrowLastError("LoadAccelerators (main menu)");

        using MenuHandle = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyOwnedMenu)>;
        MenuHandle mainMenu(LoadMenuW(instance, MAKEINTRESOURCEW(IDM_MAIN)), DestroyOwnedMenu);
        if (!mainMenu) ThrowLastError("LoadMenu (main menu)");
        MenuHandle recentMenu(CreatePopupMenu(), DestroyOwnedMenu);
        if (!recentMenu) ThrowLastError("CreatePopupMenu (recent files)");

        HMENU file = GetSubMenu(mainMenu.get(), 0);
        MENUITEMINFOW info{ sizeof(info) };
        info.fMask = MIIM_SUBMENU | MIIM_ID;
        info.hSubMenu = recentMenu.get();
        if (!SetMenuItemInfoW(file, CmdRecentFirst, FALSE, &info))
            ThrowLastError("SetMenuItemInfo (recent files)");
        recent = recentMenu.release();
        SetRecentFiles({});
        bar = mainMenu.release();
    }

    MainMenu::~MainMenu()
    {
        if (ownsBar && IsMenu(bar)) DestroyOwnedMenu(bar);
    }

    MenuAppearance::~MenuAppearance()
    {
        if (darkBackground && !DeleteObject(darkBackground)) LogError("DeleteObject (dark menu brush)");
    }

    void MenuAppearance::Apply(HMENU menu, bool topLevel, bool dark)
    {
        if (dark == darkMode) return;
        if (dark && !darkBackground)
        {
            darkBackground = CreateSolidBrush(DarkColors::Background);
            if (!darkBackground) ThrowHr("CreateSolidBrush (dark menu)", E_FAIL);
        }
        if (dark) Style(menu, topLevel);
        else RestoreItems();
        darkMode = dark;
        MENUINFO background{ sizeof(background), MIM_BACKGROUND | MIM_APPLYTOSUBMENUS };
        background.hbrBack = dark ? darkBackground : nullptr;
        if (!SetMenuInfo(menu, &background)) ThrowLastError("SetMenuInfo (menu background)");
    }

    void MenuAppearance::Style(HMENU menu, bool topLevel)
    {
        int count = GetMenuItemCount(menu);
        if (count < 0) ThrowLastError("GetMenuItemCount (menu colors)");
        entries[menu].resize(count);
        for (int i = 0; i < count; ++i)
        {
            MENUITEMINFOW info{ sizeof(info), MIIM_FTYPE | MIIM_STRING | MIIM_SUBMENU | MIIM_DATA };
            if (!GetMenuItemInfoW(menu, i, TRUE, &info)) ThrowLastError("GetMenuItemInfo (menu colors)");
            if (info.hSubMenu) Style(info.hSubMenu, false);
            if (info.fType & MFT_SEPARATOR) continue;
            auto& entry = entries.at(menu)[i];
            entry.text.resize(info.cch + 1);
            info.dwTypeData = entry.text.data();
            info.cch = static_cast<UINT>(entry.text.size());
            if (!GetMenuItemInfoW(menu, i, TRUE, &info)) ThrowLastError("GetMenuItemInfo (menu label)");
            entry.text.resize(info.cch);
            entry.topLevel = topLevel;
            entry.originalType = info.fType;
            entry.originalData = info.dwItemData;
            info.fType |= MFT_OWNERDRAW;
            info.dwItemData = reinterpret_cast<ULONG_PTR>(&entry);
            info.fMask = MIIM_FTYPE | MIIM_DATA;
            if (!SetMenuItemInfoW(menu, i, TRUE, &info)) ThrowLastError("SetMenuItemInfo (menu colors)");
            entry.styled = true;
        }
    }

    void MenuAppearance::RestoreItems()
    {
        for (auto& [menu, items] : entries)
        {
            for (UINT i = 0; i < items.size(); ++i)
            {
                auto& entry = items[i];
                if (!entry.styled) continue;
                MENUITEMINFOW info{ sizeof(info), MIIM_FTYPE | MIIM_DATA };
                info.fType = entry.originalType;
                info.dwItemData = entry.originalData;
                if (!SetMenuItemInfoW(menu, i, TRUE, &info))
                    ThrowLastError("SetMenuItemInfo (restore native menu)");
                entry.styled = false;
            }
        }
        entries.clear();
    }

    void MainMenu::SetDarkMode(HWND window, bool dark)
    {
        Apply(bar, true, dark);
        if (!DrawMenuBar(window)) ThrowLastError("DrawMenuBar (menu colors)");
    }

    void MenuAppearance::RefreshItems(HMENU menu)
    {
        if (darkMode)
        {
            // The caller has replaced all the old items before refreshing their drawing data.
            entries.erase(menu);
            Style(menu, false);
        }
    }

    bool MenuAppearance::Measure(HWND window, MEASUREITEMSTRUCT& item) const
    {
        if (item.CtlType != ODT_MENU || !item.itemData) return false;
        auto const& entry = *reinterpret_cast<Entry const*>(item.itemData);
        UINT dpi = GetDpiForWindow(window);
        NONCLIENTMETRICSW metrics{ sizeof(metrics) };
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
            ThrowLastError("SPI_GETNONCLIENTMETRICS (menu)");
        HFONT font = CreateFontIndirectW(&metrics.lfMenuFont);
        HDC dc = GetDC(window);
        if (!font || !dc)
        {
            if (dc) ReleaseDC(window, dc);
            if (font) DeleteObject(font);
            ThrowHr("GetDC/CreateFontIndirect (menu)", E_FAIL);
        }
        auto old = SelectObject(dc, font);
        RECT rect{};
        DrawTextW(dc, entry.text.c_str(), -1, &rect, DT_CALCRECT | DT_SINGLELINE | DT_EXPANDTABS);
        SelectObject(dc, old);
        ReleaseDC(window, dc);
        DeleteObject(font);
        item.itemWidth = rect.right + MulDiv(entry.topLevel ? 14 : 68, dpi, 96);
        item.itemHeight = GetSystemMetricsForDpi(SM_CYMENU, dpi);
        return true;
    }

    bool MenuAppearance::Draw(HWND window, DRAWITEMSTRUCT const& item) const
    {
        if (item.CtlType != ODT_MENU || !item.itemData) return false;
        auto const& entry = *reinterpret_cast<Entry const*>(item.itemData);
        auto dc = item.hDC;
        bool selected = (item.itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
        COLORREF background = darkMode ? (selected ? DarkColors::Selected : DarkColors::Background) :
            GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_MENU);
        COLORREF text = darkMode ? DarkColors::Text : GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT);
        if (item.itemState & (ODS_DISABLED | ODS_GRAYED))
            text = darkMode ? RGB(157, 159, 165) : GetSysColor(COLOR_GRAYTEXT);
        SetDCBrushColor(dc, background);
        FillRect(dc, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, text);
        UINT dpi = GetDpiForWindow(window);
        RECT label = item.rcItem;
        label.left += MulDiv(entry.topLevel ? 6 : 26, dpi, 96);
        label.right -= MulDiv(entry.topLevel ? 4 : 18, dpi, 96);
        auto tab = entry.text.find(L'\t');
        UINT flags = DT_SINGLELINE | DT_VCENTER | ((item.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0);
        DrawTextW(dc, entry.text.c_str(), tab == std::wstring::npos ? -1 : static_cast<int>(tab), &label, flags);
        if (tab != std::wstring::npos)
            DrawTextW(dc, entry.text.c_str() + tab + 1, -1, &label, flags | DT_RIGHT);
        if (item.itemState & ODS_CHECKED)
        {
            RECT mark = item.rcItem;
            mark.right = label.left;
            DrawTextW(dc, L"\u2713", -1, &mark, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        return true;
    }

    LRESULT MenuAppearance::MenuChar(wchar_t key, HMENU menu) const
    {
        auto found = entries.find(menu);
        if (found == entries.end()) return 0;
        int first = -1, next = -1, highlighted = -1, count = 0;
        for (int i = 0; i < static_cast<int>(found->second.size()); ++i)
        {
            UINT state = GetMenuState(menu, i, MF_BYPOSITION);
            if (state != static_cast<UINT>(-1) && state & MF_HILITE) highlighted = i;
        }
        for (int i = 0; i < static_cast<int>(found->second.size()); ++i)
        {
            auto const& text = found->second[i].text;
            for (size_t p = 0; (p = text.find(L'&', p)) != std::wstring::npos && p + 1 < text.size(); ++p)
            {
                if (text[p + 1] == L'&') { ++p; continue; }
                if (std::towlower(text[p + 1]) == std::towlower(key))
                {
                    if (first < 0) first = i;
                    if (next < 0 && i > highlighted) next = i;
                    ++count;
                }
                break;
            }
        }
        if (!count) return MAKELRESULT(0, MNC_IGNORE);
        return count == 1 ? MAKELRESULT(first, MNC_EXECUTE) : MAKELRESULT(next < 0 ? first : next, MNC_SELECT);
    }

    namespace
    {
        LRESULT CALLBACK ContextMenuProc(HWND owner, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
        {
            auto const& appearance = *reinterpret_cast<MenuAppearance const*>(data);
            try
            {
                switch (message)
                {
                case WM_MEASUREITEM:
                    if (appearance.Measure(owner, *reinterpret_cast<MEASUREITEMSTRUCT*>(lp))) return TRUE;
                    break;
                case WM_DRAWITEM:
                    if (appearance.Draw(owner, *reinterpret_cast<DRAWITEMSTRUCT const*>(lp))) return TRUE;
                    break;
                case WM_MENUCHAR:
                    if (auto result = appearance.MenuChar(static_cast<wchar_t>(LOWORD(wp)), reinterpret_cast<HMENU>(lp)))
                        return result;
                    break;
                }
            }
            catch (...)
            {
                LogException("context menu appearance");
            }
            return DefSubclassProc(owner, message, wp, lp);
        }
    }

    UINT TrackContextMenu(HMENU menu, HWND owner, POINT screen)
    {
        constexpr UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON;
        if (!GetPropW(GetAncestor(owner, GA_ROOTOWNER), DarkColors::WindowProperty))
            return static_cast<UINT>(TrackPopupMenuEx(menu, flags, screen.x, screen.y, owner, nullptr));

        MenuAppearance appearance;
        // Each caller owns a fresh menu and destroys it when this call returns.
        appearance.Apply(menu, false, true);
        UINT_PTR id = reinterpret_cast<UINT_PTR>(&appearance);
        if (!SetWindowSubclass(owner, ContextMenuProc, id, id))
            ThrowHr("SetWindowSubclass (context menu appearance)", E_FAIL);
        UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, flags, screen.x, screen.y, owner, nullptr));
        if (IsWindow(owner) && !RemoveWindowSubclass(owner, ContextMenuProc, id))
            LogError("RemoveWindowSubclass (context menu appearance)");
        return chosen;
    }

    void MainMenu::SetRecentFiles(std::vector<std::filesystem::path> const& files)
    {
        int count = GetMenuItemCount(recent);
        if (count == -1) ThrowLastError("GetMenuItemCount (recent files)");
        while (count > 0)
        {
            --count;
            if (!DeleteMenu(recent, static_cast<UINT>(count), MF_BYPOSITION))
                ThrowLastError("DeleteMenu (recent files)");
        }
        if (files.empty())
        {
            if (!AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"Keine zuletzt geöffneten Dateien"))
            {
                ThrowLastError("AppendMenu(recent files)");
            }
            RefreshItems(recent);
            return;
        }
        for (size_t i = 0; i < files.size() && CmdRecentFirst + static_cast<int>(i) <= CmdRecentLast; ++i)
        {
            // "&1 C:\...\Blatt.xopp": the number as the access key, the path shortened in the middle like Windows does,
            // & doubled; a file that is gone is shown disabled (honest: it cannot be opened).
            wchar_t compact[MAX_PATH]{};
            if (!PathCompactPathExW(compact, files[i].c_str(), 64, 0)) wcsncpy_s(compact, files[i].c_str(), _TRUNCATE);
            std::wstring path;
            for (wchar_t const* c = compact; *c; ++c)
            {
                path += *c;
                if (*c == L'&') path += L'&';
            }
            std::wstring text = i < 9 ? std::format(L"&{} {}", i + 1, path) : std::format(L"1&0 {}", path);
            bool exists = GetFileAttributesW(files[i].c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!AppendMenuW(recent, MF_STRING | (exists ? MF_ENABLED : MF_GRAYED), static_cast<UINT_PTR>(CmdRecentFirst + i), text.c_str()))
            {
                ThrowLastError("AppendMenu(recent files)");
            }
        }
        RefreshItems(recent);
    }

    void MainMenu::Enable(int command, bool enabled)
    {
        EnableMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    }

    void MainMenu::Check(int command, bool checked)
    {
        CheckMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    }
}
