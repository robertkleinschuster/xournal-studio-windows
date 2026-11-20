#include "PagesPane.h"

#include "ChildControls.h"
#include "GlyphImages.h"
#include "ThumbnailDrawing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioPagesPane";
        constexpr int ListId = 1, BarId = 2;
        constexpr int ThumbWidthDip = 150;
        enum MenuCommand : UINT { MenuBefore = 1, MenuAfter, MenuFromFile, MenuUp, MenuDown, MenuDelete };
        enum BarCommand : int { BarUp = 101, BarDown };
        constexpr wchar_t GlyphUp = 0xE74A, GlyphDown = 0xE74B; // Segoe Fluent Icons "Up" / "Down"

        int Scaled(int dip, HWND window) { return MulDiv(dip, static_cast<int>(GetDpiForWindow(window)), 96); }

    }

    PagesPane::PagesPane(HWND parent, PageThumbnails& thumbnails) : thumbnails(thumbnails)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        if (!GetClassInfoExW(instance, ClassName, &wc))
        {
            wc = { sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = ClassName;
            if (!RegisterClassExW(&wc)) ThrowLastError("RegisterClassEx(pages pane)");
        }
        window = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, instance, this);
        if (!window) ThrowLastError("CreateWindowEx(pages pane)");
        CreateBar();
        list =CreateWindowExW(0, WC_LISTVIEWW, L"Seiten", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SHAREIMAGELISTS |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_AUTOARRANGE | LVS_ALIGNTOP, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ListId)), instance, nullptr);
        if (!list) ThrowLastError("CreateWindowEx(pages list)");
        HRESULT hr = SetWindowTheme(list, L"Explorer", nullptr); // File Explorer's selection look
        if (FAILED(hr)) LogError(std::format("pages pane: theme not set: {}", DescribeHr(hr)));
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER);
        UpdateSizes();
    }

    PagesPane::~PagesPane()
    {
        if (window && IsWindow(window)) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (images) ImageList_Destroy(images);
        if (barImages) ImageList_Destroy(barImages);
    }

    void PagesPane::CreateBar()
    {
        bar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"Seite verschieben", WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT |
            TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(BarId)), GetModuleHandleW(nullptr), nullptr);
        if (!bar) ThrowLastError("CreateWindowEx(pages toolbar)");
        SendMessageW(bar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(bar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
        ChildControls::UseMessageFont(bar);
        TBBUTTON buttons[2]{};
        wchar_t const* texts[] = { L"Nach oben", L"Nach unten" };
        int ids[] = { BarUp, BarDown };
        for (int i = 0; i < 2; ++i)
        {
            buttons[i].iBitmap = i;
            buttons[i].idCommand = ids[i];
            buttons[i].fsState = 0; // enabled once a page is selected
            buttons[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
            buttons[i].iString = reinterpret_cast<INT_PTR>(texts[i]);
        }
        if (!SendMessageW(bar, TB_ADDBUTTONSW, 2, reinterpret_cast<LPARAM>(buttons))) ThrowLastError("TB_ADDBUTTONS (pages)");
    }

    void PagesPane::UpdateSizes()
    {
        HIMAGELIST glyphs = CreateGlyphImages({ GlyphUp, GlyphDown }, Scaled(16, window));
        SendMessageW(bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(glyphs));
        if (barImages) ImageList_Destroy(barImages);
        barImages = glyphs;
        ChildControls::UseMessageFont(bar);
        SendMessageW(bar, TB_AUTOSIZE, 0, 0);

        // One blank image of an A4 thumbnail's size gives every item its place; the thumbnail is drawn over it.
        thumbWidth = Scaled(ThumbWidthDip, window);
        thumbHeight = static_cast<int>(std::lround(thumbWidth * 1.4142));
        HIMAGELIST created = ImageList_Create(thumbWidth, thumbHeight, ILC_COLOR32, 1, 0);
        if (!created) ThrowLastError("ImageList_Create (pages)");
        HDC screen = GetDC(nullptr);
        HBITMAP blank = CreateCompatibleBitmap(screen, thumbWidth, thumbHeight);
        ReleaseDC(nullptr, screen);
        ImageList_Add(created, blank, nullptr);
        DeleteObject(blank);
        ListView_SetImageList(list, created, LVSIL_NORMAL);
        if (images) ImageList_Destroy(images);
        images = created;
    }

    void PagesPane::Layout()
    {
        RECT rc;
        GetClientRect(window, &rc);
        int gap = Scaled(4, window);
        int barHeight = HIWORD(SendMessageW(bar, TB_GETBUTTONSIZE, 0, 0));
        MoveWindow(bar, gap, gap, rc.right - 2 * gap, barHeight, TRUE);
        int top = barHeight + 2 * gap;
        MoveWindow(list, 0, top, rc.right, std::max(0, static_cast<int>(rc.bottom) - top), TRUE);
        // One column: an item as wide as the list.
        int width = std::max(thumbWidth + Scaled(16, window), static_cast<int>(rc.right) - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window)));
        ListView_SetIconSpacing(list, width, thumbHeight + Scaled(36, window));
        ListView_Arrange(list, LVA_DEFAULT);
    }

    void PagesPane::SetCount(int pages)
    {
        // Ordinary items whose text and image come from LVN_GETDISPINFO.
        selecting = true;
        for (int n = ListView_GetItemCount(list); n > pages; --n)
        {
            if (!ListView_DeleteItem(list, n - 1)) LogError(std::format("pages pane: item {} not deleted", n));
        }
        for (int n = ListView_GetItemCount(list); n < pages; ++n)
        {
            LVITEMW item{ LVIF_TEXT | LVIF_IMAGE, n };
            item.pszText = LPSTR_TEXTCALLBACKW;
            item.iImage = I_IMAGECALLBACK;
            if (ListView_InsertItem(list, &item) < 0) LogError(std::format("pages pane: item {} not inserted", n + 1));
        }
        selecting = false;
        count = ListView_GetItemCount(list);
        InvalidateRect(list, nullptr, TRUE);
        UpdateButtons();
    }

    void PagesPane::Select(int index)
    {
        if (index < 0 || index >= count || Selected() == index) return;
        selecting = true;
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, index, FALSE);
        selecting = false;
        UpdateButtons();
    }

    void PagesPane::UpdateButtons()
    {
        int selected = Selected();
        SendMessageW(bar, TB_ENABLEBUTTON, BarUp, selected > 0);
        SendMessageW(bar, TB_ENABLEBUTTON, BarDown, selected >= 0 && selected + 1 < count);
    }

    void PagesPane::MoveBy(int index, int delta)
    {
        int to = index + delta;
        Log(std::format("pages pane: page {} to {}", index + 1, to + 1));
        if (index < 0 || index >= count || to < 0 || to >= count || !move) return;
        move(index, to);
    }

    void PagesPane::Redraw(int index)
    {
        if (index >= 0 && index < count) ListView_RedrawItems(list, index, index);
    }

    int PagesPane::Selected() const
    {
        return ListView_GetNextItem(list, -1, LVNI_SELECTED);
    }

    void PagesPane::DrawThumbnail(HDC dc, RECT const& area, int index)
    {
        auto layers = thumbnails.Of(index);
        DrawPageThumbnail(dc, area, layers.aspect, layers.background.get(), layers.content.get());
    }

    LRESULT PagesPane::OnCustomDraw(NMLVCUSTOMDRAW& draw)
    {
        switch (draw.nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYPOSTPAINT;
        case CDDS_ITEMPOSTPAINT:
        {
            int index = static_cast<int>(draw.nmcd.dwItemSpec);
            RECT icon{ LVIR_ICON };
            if (ListView_GetItemRect(list, index, &icon, LVIR_ICON))
            {
                InflateRect(&icon, -Scaled(4, window), -Scaled(4, window));
                DrawThumbnail(draw.nmcd.hdc, icon, index);
            }
            return CDRF_DODEFAULT;
        }
        }
        return CDRF_DODEFAULT;
    }

    void PagesPane::ShowMenu(int index, POINT screen)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu) ThrowLastError("CreatePopupMenu (pages)");
        AppendMenuW(menu, MF_STRING, MenuBefore, L"Leere Seite da&vor einfügen");
        AppendMenuW(menu, MF_STRING, MenuAfter, L"Leere Seite dana&ch einfügen");
        AppendMenuW(menu, MF_STRING, MenuFromFile, L"Seiten aus &PDF oder Dokument einfügen…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (index > 0 ? 0 : MF_GRAYED), MenuUp, L"Seite nach &oben");
        AppendMenuW(menu, MF_STRING | (index + 1 < count ? 0 : MF_GRAYED), MenuDown, L"Seite nach &unten");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (count > 1 ? 0 : MF_GRAYED), MenuDelete, L"Seite &löschen…");
        Log(std::format("pages pane: menu of page {}", index + 1));
        UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, window, nullptr);
        DestroyMenu(menu);
        Log(std::format("pages pane: menu of page {}: {}", index + 1, chosen));
        switch (chosen)
        {
        case MenuBefore: if (insertBlank) insertBlank(index); break;
        case MenuAfter: if (insertBlank) insertBlank(index + 1); break;
        case MenuFromFile: if (insertFromFile) insertFromFile(index); break;
        case MenuUp: MoveBy(index, -1); break;
        case MenuDown: MoveBy(index, 1); break;
        case MenuDelete: if (remove) remove(index); break;
        default: break;
        }
    }

    LRESULT PagesPane::OnNotify(NMHDR const& header)
    {
        if (header.hwndFrom != list) return 0;
        switch (header.code)
        {
        case LVN_GETDISPINFOW:
        {
            auto& info = const_cast<NMLVDISPINFOW&>(reinterpret_cast<NMLVDISPINFOW const&>(header));
            if (info.item.mask & LVIF_TEXT)
            {
                std::wstring text = std::format(L"Seite {}", info.item.iItem + 1);
                wcsncpy_s(info.item.pszText, info.item.cchTextMax, text.c_str(), _TRUNCATE);
            }
            if (info.item.mask & LVIF_IMAGE) info.item.iImage = 0;
            return 0;
        }
        case LVN_ITEMCHANGED:
        {
            auto const& change = reinterpret_cast<NMLISTVIEW const&>(header);
            bool nowSelected = (change.uNewState & LVIS_SELECTED) && !(change.uOldState & LVIS_SELECTED);
            if (change.uChanged & LVIF_STATE) UpdateButtons();
            if (nowSelected && !selecting && change.iItem >= 0 && goTo) goTo(change.iItem);
            return 0;
        }
        case NM_RCLICK:
        {
            auto const& click = reinterpret_cast<NMITEMACTIVATE const&>(header);
            if (click.iItem < 0) return 0;
            POINT screen = click.ptAction;
            ClientToScreen(list, &screen);
            ShowMenu(click.iItem, screen);
            return TRUE;
        }
        case NM_CUSTOMDRAW:
            return OnCustomDraw(const_cast<NMLVCUSTOMDRAW&>(reinterpret_cast<NMLVCUSTOMDRAW const&>(header)));
        }
        return 0;
    }

    LRESULT PagesPane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_DPICHANGED_AFTERPARENT:
            UpdateSizes();
            Layout();
            return 0;
        case WM_NOTIFY:
            return OnNotify(*reinterpret_cast<NMHDR const*>(lp));
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lp) == bar)
            {
                if (LOWORD(wp) == BarUp) MoveBy(Selected(), -1);
                else if (LOWORD(wp) == BarDown) MoveBy(Selected(), 1);
                return 0;
            }
            break;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK PagesPane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<PagesPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->window || !self->list) return DefWindowProcW(hwnd, message, wp, lp);
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("pages pane: message 0x{:X}", message));
            return 0;
        }
    }
}
