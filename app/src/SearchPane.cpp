#include "SearchPane.h"

#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>

#include <algorithm>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioSearchPane";
        constexpr int FieldId = 1, StatusId = 2, ListId = 3;
        constexpr int FieldHeightDip = 26, StatusHeightDip = 22, GapDip = 6, PageColumnDip = 52, KindColumnDip = 92;

        wchar_t const* KindName(search::HitKind kind)
        {
            switch (kind)
            {
            case search::HitKind::Pdf: return L"PDF";
            case search::HitKind::Text: return L"Text";
            default: return L"Handschrift";
            }
        }

        int Dip(HWND window, int dip) { return MulDiv(dip, static_cast<int>(GetDpiForWindow(window)), 96); }
    }

    SearchPane::SearchPane(HWND parent)
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
            if (!RegisterClassExW(&wc)) ThrowLastError("RegisterClassEx(search pane)");
        }
        window = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, instance, this);
        if (!window) ThrowLastError("CreateWindowEx(search pane)");
        ChildControls::PendingPaneWindow pending(window);
        field = CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(FieldId)), instance, nullptr);
        if (!field) ThrowLastError("CreateWindowEx(search field)");
        Edit_SetCueBannerTextFocused(field, L"Suchen: PDF, Texte, Handschrift", TRUE);
        status = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusId)), instance, nullptr);
        if (!status) ThrowLastError("CreateWindowEx(search status)");
        list = CreateWindowExW(0, WC_LISTVIEWW, L"Treffer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ListId)), instance, nullptr);
        if (!list) ThrowLastError("CreateWindowEx(search hits)");
        HRESULT hr = SetWindowTheme(list, L"Explorer", nullptr);
        if (FAILED(hr)) LogError(std::format("search pane: theme not set: {}", DescribeHr(hr)));
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
        wchar_t const* titles[] = { L"Seite", L"Art", L"Fundstelle" };
        int widths[] = { PageColumnDip, KindColumnDip, 400 };
        for (int i = 0; i < 3; ++i)
        {
            LVCOLUMNW column{ LVCF_TEXT | LVCF_WIDTH };
            column.pszText = const_cast<wchar_t*>(titles[i]);
            column.cx = Dip(window, widths[i]);
            if (ListView_InsertColumn(list, i, &column) < 0) ThrowHr("LVM_INSERTCOLUMN (search)", E_FAIL);
        }
        for (HWND control : { field, status, list }) ChildControls::UseMessageFont(control);
        if (!SetWindowSubclass(field, FieldProc, 1, reinterpret_cast<DWORD_PTR>(this)))
            ThrowHr("SetWindowSubclass (search field)", E_FAIL);
        pending.Release();
    }

    SearchPane::~SearchPane()
    {
        if (IsWindow(field) && !RemoveWindowSubclass(field, FieldProc, 1))
            LogError("RemoveWindowSubclass (search field) failed");
        if (window && IsWindow(window)) ChildControls::SetWindowData(window, nullptr);
    }

    void SearchPane::FocusField()
    {
        SetFocus(field);
        Edit_SetSel(field, 0, -1);
    }

    std::wstring SearchPane::FieldText() const
    {
        std::wstring text(static_cast<size_t>(GetWindowTextLengthW(field)) + 1, L'\0');
        text.resize(static_cast<size_t>(GetWindowTextW(field, text.data(), static_cast<int>(text.size()))));
        return text;
    }

    void SearchPane::Show(std::vector<search::SearchHit> const& hits, std::optional<size_t> current, bool running,
        std::wstring const& query)
    {
        searched = query;
        rows.clear();
        rows.reserve(hits.size());
        for (auto const& hit : hits)
        {
            rows.push_back({ std::to_wstring(hit.page + 1), KindName(hit.kind), hit.context });
        }
        std::wstring text;
        if (query.empty()) text = L"";
        else if (running) text = std::format(L"Sucht … {} Treffer bisher", hits.size());
        else if (hits.empty()) text = L"Keine Treffer";
        else if (current) text = std::format(L"Treffer {} von {}", *current + 1, hits.size());
        else text = std::format(L"{} Treffer", hits.size());
        if (!SetWindowTextW(status, text.c_str())) ThrowLastError("SetWindowText (search status)");
        filling = true;
        ListView_SetItemCountEx(list, static_cast<int>(rows.size()), LVSICF_NOSCROLL);
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (current && *current < rows.size())
        {
            int index = static_cast<int>(*current);
            ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, index, FALSE);
        }
        InvalidateRect(list, nullptr, FALSE);
        filling = false;
    }

    bool SearchPane::OnFieldKey(WPARAM key)
    {
        bool shift = GetKeyState(VK_SHIFT) < 0;
        if (key == VK_RETURN || key == VK_F3)
        {
            std::wstring query = FieldText();
            if (query != searched || searched.empty())
            {
                if (key == VK_RETURN && searchRequested) searchRequested(query);
            }
            else if (shift)
            {
                if (previous) previous();
            }
            else if (next)
            {
                next();
            }
            return true;
        }
        if (key == VK_ESCAPE)
        {
            if (leave) leave();
            return true;
        }
        return false;
    }

    void SearchPane::Layout()
    {
        RECT rc{};
        if (!GetClientRect(window, &rc)) ThrowLastError("GetClientRect (search pane)");
        int gap = Dip(window, GapDip), fieldHeight = Dip(window, FieldHeightDip), statusHeight = Dip(window, StatusHeightDip);
        int width = std::max(0, static_cast<int>(rc.right) - 2 * gap);
        if (!MoveWindow(field, gap, gap, width, fieldHeight, TRUE)) ThrowLastError("MoveWindow (search field)");
        if (!MoveWindow(status, gap, gap * 2 + fieldHeight, width, statusHeight, TRUE))
            ThrowLastError("MoveWindow (search status)");
        int top = gap * 2 + fieldHeight + statusHeight;
        if (!MoveWindow(list, 0, top, rc.right, std::max(0, static_cast<int>(rc.bottom) - top), TRUE))
            ThrowLastError("MoveWindow (search results)");
        // The hit's text gets what the page and kind columns leave.
        int rest = rc.right - Dip(window, PageColumnDip) - Dip(window, KindColumnDip) - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window));
        if (!ListView_SetColumnWidth(list, 2, std::max(rest, Dip(window, 120))))
            ThrowHr("LVM_SETCOLUMNWIDTH (search results)", E_FAIL);
    }

    LRESULT SearchPane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_DPICHANGED_AFTERPARENT:
            for (HWND control : { field, status, list }) ChildControls::UseMessageFont(control);
            Layout();
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_SETFOCUS:
            SetFocus(field);
            return 0;
        case WM_CTLCOLORSTATIC:
            // The status line on the pane's own background (white), not the dialog color.
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_NOTIFY:
        {
            auto const& header = *reinterpret_cast<NMHDR const*>(lp);
            if (header.hwndFrom != list) break;
            if (header.code == LVN_GETDISPINFOW)
            {
                auto& info = const_cast<NMLVDISPINFOW&>(reinterpret_cast<NMLVDISPINFOW const&>(header));
                if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 && static_cast<size_t>(info.item.iItem) < rows.size())
                {
                    auto const& row = rows[static_cast<size_t>(info.item.iItem)];
                    std::wstring const& text = info.item.iSubItem == 0 ? row.page : info.item.iSubItem == 1 ? row.kind : row.context;
                    wcsncpy_s(info.item.pszText, info.item.cchTextMax, text.c_str(), _TRUNCATE);
                }
                return 0;
            }
            if (header.code == LVN_ITEMCHANGED)
            {
                auto const& change = reinterpret_cast<NMLISTVIEW const&>(header);
                bool nowSelected = (change.uNewState & LVIS_SELECTED) && !(change.uOldState & LVIS_SELECTED);
                if (nowSelected && !filling && change.iItem >= 0 && chosen) chosen(static_cast<size_t>(change.iItem));
                return 0;
            }
            break;
        }
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK SearchPane::FieldProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<SearchPane*>(data);
        try
        {
            if (message == WM_KEYDOWN && self->OnFieldKey(wp)) return 0;
            // An edit control beeps on Enter and Esc as characters: they were handled as keys.
            if (message == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
        }
        catch (...)
        {
            LogException(std::format("search field: message 0x{:X}", message));
            return 0;
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }

    LRESULT CALLBACK SearchPane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            if (!ChildControls::SetWindowData(hwnd, reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)) return FALSE;
        }
        auto self = reinterpret_cast<SearchPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->list) return DefWindowProcW(hwnd, message, wp, lp);
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("search pane: message 0x{:X}", message));
            return 0;
        }
    }
}
