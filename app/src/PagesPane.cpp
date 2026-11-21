#include "PagesPane.h"

#include "ChildControls.h"
#include "ThumbnailDrawing.h"

#include "inkpages/Toolbar.h"
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

        HIMAGELIST PageButtonImages(int size)
        {
            std::unique_ptr<std::remove_pointer_t<HIMAGELIST>, decltype(&ImageList_Destroy)> images(
                ImageList_Create(size, size, ILC_COLOR32, 2, 0), ImageList_Destroy);
            if (!images) ThrowHr("ImageList_Create (page buttons)", E_FAIL);
            for (wchar_t glyph : { GlyphUp, GlyphDown })
            {
                HBITMAP bitmap = inkpages::CreateFluentGlyphBitmap(glyph, GetSysColor(COLOR_BTNTEXT), size);
                if (!bitmap) ThrowHr("CreateFluentGlyphBitmap (page buttons)", E_FAIL);
                int index = ImageList_Add(images.get(), bitmap, nullptr);
                if (!DeleteObject(bitmap)) LogError("DeleteObject (page button glyph) failed");
                if (index < 0) ThrowHr("ImageList_Add (page buttons)", E_FAIL);
            }
            return images.release();
        }
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
        ChildControls::PendingPaneWindow pending(window);
        CreateBar();
        list = CreateWindowExW(0, WC_LISTVIEWW, L"Seiten", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SHAREIMAGELISTS |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ListId)), instance, nullptr);
        if (!list) ThrowLastError("CreateWindowEx(pages list)");
        HRESULT hr = SetWindowTheme(list, L"Explorer", nullptr); // File Explorer's selection look
        if (FAILED(hr)) LogError(std::format("pages pane: theme not set: {}", DescribeHr(hr)));
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER);
        if (!SetWindowSubclass(list, ListProc, 0, reinterpret_cast<DWORD_PTR>(this)))
            ThrowHr("SetWindowSubclass (page thumbnails)", E_FAIL);
        try
        {
            UpdateSizes();
        }
        catch (...)
        {
            ReleaseImageLists();
            throw;
        }
        pending.Release();
    }

    PagesPane::~PagesPane()
    {
        if (IsWindow(list)) RemoveWindowSubclass(list, ListProc, 0);
        if (window && IsWindow(window)) ChildControls::SetWindowData(window, nullptr);
        ReleaseImageLists();
    }

    void PagesPane::ReleaseImageLists()
    {
        if (images)
        {
            if (IsWindow(list)) ListView_SetImageList(list, nullptr, LVSIL_NORMAL);
            if (!ImageList_Destroy(images)) LogError("ImageList_Destroy (page thumbnails) failed");
            images = nullptr;
        }
        if (barImages)
        {
            if (IsWindow(bar)) SendMessageW(bar, TB_SETIMAGELIST, 0, 0);
            if (!ImageList_Destroy(barImages)) LogError("ImageList_Destroy (page toolbar) failed");
            barImages = nullptr;
        }
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
        HIMAGELIST glyphs = PageButtonImages(Scaled(16, window));
        SendMessageW(bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(glyphs));
        if (barImages) ImageList_Destroy(barImages);
        barImages = glyphs;
        ChildControls::UseMessageFont(bar);
        SendMessageW(bar, TB_AUTOSIZE, 0, 0);

        // The native image slot establishes hit testing and leaves the page label below the preview.
        thumbWidth = Scaled(ThumbWidthDip, window);
        thumbHeight = static_cast<int>(std::lround(thumbWidth * 1.4142));
        HIMAGELIST created = CreateSlotImages({ thumbWidth, thumbHeight });
        ListView_SetImageList(list, created, LVSIL_NORMAL);
        if (images) ImageList_Destroy(images);
        images = created;
        thumbnails.SetBox(Box());
    }

    SIZE PagesPane::Box() const
    {
        int margin = Scaled(4, window);
        return { thumbWidth - 2 * margin, thumbHeight - 2 * margin };
    }

    void PagesPane::Layout()
    {
        RECT rc{};
        if (!GetClientRect(window, &rc)) ThrowLastError("GetClientRect (pages pane)");
        int gap = Scaled(4, window);
        int barHeight = HIWORD(SendMessageW(bar, TB_GETBUTTONSIZE, 0, 0));
        MoveWindow(bar, gap, gap, rc.right - 2 * gap, barHeight, TRUE);
        int top = barHeight + 2 * gap;
        MoveWindow(list, 0, top, rc.right, std::max(0, static_cast<int>(rc.bottom) - top), TRUE);
        int width = rc.right - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(list));
        int x = std::max(Scaled(4, window), (width - thumbWidth) / 2);
        int pitch = thumbHeight + Scaled(32, window);
        if (x != itemX || pitch != itemPitch)
        {
            itemX = x;
            itemPitch = pitch;
            ListView_SetIconSpacing(list, thumbWidth + Scaled(8, window), pitch);
            for (int index = 0; index < count; ++index) PositionItem(index);
        }
        UpdateThumbnailFocus();
    }

    void PagesPane::PositionItem(int index)
    {
        ListView_SetItemPosition32(list, index, itemX, Scaled(4, window) + index * itemPitch);
    }

    void PagesPane::SetCount(int pages)
    {
        // Ordinary items whose labels come from LVN_GETDISPINFO.
        selecting = true;
        for (int n = ListView_GetItemCount(list); n > pages; --n)
        {
            if (!ListView_DeleteItem(list, n - 1)) LogError(std::format("pages pane: item {} not deleted", n));
        }
        for (int n = ListView_GetItemCount(list); n < pages; ++n)
        {
            LVITEMW item{ LVIF_TEXT | LVIF_IMAGE, n };
            item.pszText = LPSTR_TEXTCALLBACKW;
            item.iImage = 0;
            int inserted = ListView_InsertItem(list, &item);
            if (inserted < 0) LogError(std::format("pages pane: item {} not inserted", n + 1));
            else PositionItem(inserted);
        }
        selecting = false;
        count = ListView_GetItemCount(list);
        UpdateThumbnailFocus();
        InvalidateRect(list, nullptr, TRUE);
        UpdateButtons();
    }

    void PagesPane::Select(int index)
    {
        if (index < 0 || index >= count || Selected() == index) return;
        selecting = true;
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, index, FALSE);
        UpdateThumbnailFocus();
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
        DrawCachedPageThumbnail(dc, area, Box(), layers.aspect, layers.background.get(), layers.content.get());
    }

    void PagesPane::UpdateThumbnailFocus()
    {
        int first = 0, end = count;
        while (first < end)
        {
            int index = first + (end - first) / 2;
            RECT bounds{};
            if (!ListView_GetItemRect(list, index, &bounds, LVIR_BOUNDS))
                ThrowHr("LVM_GETITEMRECT (page thumbnails)", E_FAIL);
            if (bounds.bottom <= 0) first = index + 1;
            else end = index;
        }
        thumbnails.SetPaneFocus(first < count ? first : -1);
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
            RECT preview{};
            if (ListView_GetItemRect(list, index, &preview, LVIR_ICON))
            {
                DrawThumbnail(draw.nmcd.hdc, preview, index);
            }
            return CDRF_DODEFAULT;
        }
        }
        return CDRF_DODEFAULT;
    }

    LRESULT CALLBACK PagesPane::ListProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
    {
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ListProc, id);
        if (message != WM_MOUSEWHEEL || GET_KEYSTATE_WPARAM(wp) & (MK_CONTROL | MK_SHIFT))
            return DefSubclassProc(hwnd, message, wp, lp);
        auto& pane = *reinterpret_cast<PagesPane*>(data);
        try
        {
            UINT lines = 0;
            if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
                ThrowLastError("SPI_GETWHEELSCROLLLINES (page thumbnails)");
            RECT viewport{};
            if (!GetClientRect(hwnd, &viewport)) ThrowLastError("GetClientRect (page thumbnails)");
            int pixels = lines == WHEEL_PAGESCROLL ? viewport.bottom
                : static_cast<int>(std::min<long long>(viewport.bottom, static_cast<long long>(lines) * Scaled(16, hwnd)));
            pane.wheelRemainder -= GET_WHEEL_DELTA_WPARAM(wp) * pixels;
            int dy = pane.wheelRemainder / WHEEL_DELTA;
            pane.wheelRemainder %= WHEEL_DELTA;
            if (dy && !ListView_Scroll(hwnd, 0, dy)) LogError("LVM_SCROLL (page thumbnails) failed");
            return 0;
        }
        catch (...)
        {
            LogException("pages pane: wheel scroll");
            return DefSubclassProc(hwnd, message, wp, lp);
        }
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
        case LVN_ENDSCROLL:
            UpdateThumbnailFocus();
            return 0;
        case LVN_GETDISPINFOW:
        {
            auto& info = const_cast<NMLVDISPINFOW&>(reinterpret_cast<NMLVDISPINFOW const&>(header));
            if ((info.item.mask & LVIF_TEXT) && info.item.iSubItem == 0)
            {
                std::wstring text = std::format(L"Seite {}", info.item.iItem + 1);
                wcsncpy_s(info.item.pszText, info.item.cchTextMax, text.c_str(), _TRUNCATE);
            }
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
            if (!ChildControls::SetWindowData(hwnd, reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)) return FALSE;
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
