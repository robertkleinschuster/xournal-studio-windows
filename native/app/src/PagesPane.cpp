#include "PagesPane.h"

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
        constexpr int ListId = 1;
        constexpr int ThumbWidthDip = 150;
        enum MenuCommand : UINT { MenuBefore = 1, MenuAfter, MenuFromFile, MenuUp, MenuDown, MenuDelete };

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
        list = CreateWindowExW(0, WC_LISTVIEWW, L"Seiten", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_OWNERDATA | LVS_SHAREIMAGELISTS |
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
    }

    void PagesPane::UpdateSizes()
    {
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
        MoveWindow(list, 0, 0, rc.right, rc.bottom, TRUE);
        // One column: an item as wide as the list.
        int width = std::max(thumbWidth + Scaled(16, window), static_cast<int>(rc.right) - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window)));
        ListView_SetIconSpacing(list, width, thumbHeight + Scaled(36, window));
        ListView_Arrange(list, LVA_DEFAULT);
    }

    void PagesPane::SetCount(int pages)
    {
        count = pages;
        ListView_SetItemCountEx(list, count, 0);
        InvalidateRect(list, nullptr, TRUE);
    }

    void PagesPane::Select(int index)
    {
        if (index < 0 || index >= count || Selected() == index) return;
        selecting = true;
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, index, FALSE);
        selecting = false;
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
        AppendMenuW(menu, MF_STRING | (index > 0 ? 0 : MF_GRAYED), MenuUp, L"Nach &oben");
        AppendMenuW(menu, MF_STRING | (index + 1 < count ? 0 : MF_GRAYED), MenuDown, L"Nach &unten");
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
        case MenuUp: if (move && index > 0) move(index, index - 1); break;
        case MenuDown: if (move && index + 1 < count) move(index, index + 1); break;
        case MenuDelete: if (remove) remove(index); break;
        default: break;
        }
    }

    void PagesPane::DragMove(POINT client)
    {
        POINT at = client;
        MapWindowPoints(window, list, &at, 1);
        LVINSERTMARK mark{ sizeof(mark) };
        ListView_InsertMarkHitTest(list, &at, &mark);
        ListView_SetInsertMark(list, &mark);
    }

    void PagesPane::DragDrop()
    {
        LVINSERTMARK mark{ sizeof(mark) };
        ListView_GetInsertMark(list, &mark);
        LVINSERTMARK none{ sizeof(none), 0, -1, 0 };
        ListView_SetInsertMark(list, &none);
        int from = dragFrom;
        dragFrom = -1;
        Log(std::format("pages pane: drop of page {} at mark {} ({})", from + 1, mark.iItem, (mark.dwFlags & LVIM_AFTER) ? "after" : "before"));
        if (mark.iItem < 0 || from < 0) return;
        // The mark is before or after an item; the page lands there (counted without itself).
        int target = mark.iItem + ((mark.dwFlags & LVIM_AFTER) ? 1 : 0);
        int to = target > from ? target - 1 : target;
        if (to != from && move) move(from, to);
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
        case LVN_BEGINDRAG:
            dragFrom = reinterpret_cast<NMLISTVIEW const&>(header).iItem;
            Log(std::format("pages pane: dragging page {}", dragFrom + 1));
            SetCapture(window);
            return 0;
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
        case WM_MOUSEMOVE:
            if (dragFrom >= 0) DragMove({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
            return 0;
        case WM_LBUTTONUP:
            if (dragFrom >= 0)
            {
                ReleaseCapture(); // WM_CAPTURECHANGED drops it
                DragDrop();
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (dragFrom >= 0)
            {
                LVINSERTMARK none{ sizeof(none), 0, -1, 0 };
                ListView_SetInsertMark(list, &none);
                dragFrom = -1; // capture lost (Escape, another window): no move
            }
            return 0;
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
