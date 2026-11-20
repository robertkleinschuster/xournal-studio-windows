#include "SidePane.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioSidePane";
        constexpr int TabsId = 1, TreeId = 2, SyncId = 3;
        constexpr int SyncButtonHeightDip = 32;
        constexpr wchar_t const* TabTitles[] = { L"Dateien", L"Aufgaben", L"Seiten", L"Suche", L"Gliederung" };
        constexpr int OutlineTab = 4, FixedTabs = 4; // Suche is SidePane::SearchTab

        HWND Placeholder(HWND parent, wchar_t const* text)
        {
            HWND label = CreateWindowExW(0, WC_STATICW, text, WS_CHILD | SS_CENTER, 0, 0, 0, 0, parent, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            if (!label)
            {
                ThrowLastError("CreateWindowEx(placeholder)");
            }
            return label;
        }

        // The controls use the window's message font (comctl32 v6 does not pick it up by itself).
        void UseMessageFont(HWND control)
        {
            NONCLIENTMETRICSW metrics{ sizeof(metrics) };
            if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, GetDpiForWindow(control)))
            {
                static HFONT font = CreateFontIndirectW(&metrics.lfMessageFont); // one per process, kept until exit
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            else
            {
                LogError(std::format("side pane: message font: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            }
        }
    }

    SidePane::SidePane(HWND parent, TasksPane::Messages messages)
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
            if (!RegisterClassExW(&wc))
            {
                ThrowLastError("RegisterClassEx(side pane)");
            }
        }
        window = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, instance, this);
        if (!window)
        {
            ThrowLastError("CreateWindowEx(side pane)");
        }
        tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(TabsId)), instance, nullptr);
        if (!tabs)
        {
            ThrowLastError("CreateWindowEx(tab control)");
        }
        UseMessageFont(tabs);
        for (int i = 0; i < FixedTabs; ++i)
        {
            TCITEMW item{ TCIF_TEXT };
            item.pszText = const_cast<wchar_t*>(TabTitles[i]);
            TabCtrl_InsertItem(tabs, i, &item);
        }
        explorer = std::make_unique<ExplorerTree>(window, TreeId);
        UseMessageFont(explorer->Window());
        syncButton = CreateWindowExW(0, WC_BUTTONW, L"Synchronisieren", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SyncId)), instance, nullptr);
        if (!syncButton)
        {
            ThrowLastError("CreateWindowEx(sync button)");
        }
        UseMessageFont(syncButton);
        syncTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
        if (!syncTip)
        {
            ThrowLastError("CreateWindowEx(sync tooltip)");
        }
        TTTOOLINFOW tool{ sizeof(tool) };
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window;
        tool.uId = reinterpret_cast<UINT_PTR>(syncButton);
        tool.lpszText = const_cast<wchar_t*>(L"Synchronisieren");
        if (!SendMessageW(syncTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)))
        {
            ThrowLastError("TTM_ADDTOOL(sync button)");
        }
        SendMessageW(syncTip, TTM_SETMAXTIPWIDTH, 0, MulDiv(320, static_cast<int>(GetDpiForWindow(window)), 96)); // lines
        tasks = std::make_unique<TasksPane>(window, std::move(messages));
        pagesContent = Placeholder(window, L"Seitenübersicht folgt.");
        UseMessageFont(pagesContent);
        ShowPage(0);
    }

    SidePane::~SidePane()
    {
        if (window && IsWindow(window))
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    }

    void SidePane::SetPagesContent(HWND content)
    {
        DestroyWindow(pagesContent);
        pagesContent = content;
        ShowPage(Tab());
        Layout();
    }

    void SidePane::SetSearchContent(HWND content)
    {
        searchContent = content;
        ShowPage(Tab());
        Layout();
    }

    void SidePane::SetOutlineContent(HWND content)
    {
        outlineContent = content;
        ShowPage(Tab());
        Layout();
    }

    void SidePane::ShowOutlineTab(bool shown)
    {
        if (shown == outlineShown) return;
        outlineShown = shown;
        if (shown)
        {
            TCITEMW item{ TCIF_TEXT };
            item.pszText = const_cast<wchar_t*>(TabTitles[OutlineTab]);
            if (TabCtrl_InsertItem(tabs, OutlineTab, &item) < 0) ThrowLastError("TabCtrl_InsertItem (Gliederung)");
            Layout(); // a new row of tabs may change the content's area
            return;
        }
        bool wasShown = Tab() == OutlineTab;
        TabCtrl_DeleteItem(tabs, OutlineTab);
        if (wasShown)
        {
            SetTab(0);
            if (tabChanged) tabChanged(0);
        }
        Layout();
    }

    int SidePane::Tab() const { return TabCtrl_GetCurSel(tabs); }

    int SidePane::TabsWidth() const
    {
        int count = TabCtrl_GetItemCount(tabs);
        RECT first{}, last{};
        if (count <= 0 || !TabCtrl_GetItemRect(tabs, 0, &first) || !TabCtrl_GetItemRect(tabs, count - 1, &last))
        {
            ThrowHr("TCM_GETITEMRECT (side pane tabs)", E_FAIL);
        }
        int width = last.right - first.left; // independent of how far the strip is scrolled
        if (!outlineShown)
        {
            // Gliederung's tab as wide as the control would make it: its text plus the padding the first tab has.
            HDC dc = GetDC(tabs);
            if (!dc) ThrowHr("GetDC (side pane tabs)", E_FAIL);
            HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(tabs, WM_GETFONT, 0, 0)));
            SIZE outline{}, firstText{};
            bool measured = GetTextExtentPoint32W(dc, TabTitles[OutlineTab], static_cast<int>(wcslen(TabTitles[OutlineTab])), &outline) &&
                GetTextExtentPoint32W(dc, TabTitles[0], static_cast<int>(wcslen(TabTitles[0])), &firstText);
            if (old) SelectObject(dc, old);
            ReleaseDC(tabs, dc);
            if (!measured) ThrowHr("GetTextExtentPoint32 (side pane tabs)", E_FAIL);
            width += outline.cx + (first.right - first.left - firstText.cx);
        }
        // The selected tab stands out a little on both sides; the strip's border.
        int dpi = static_cast<int>(GetDpiForWindow(tabs));
        return width + 2 * GetSystemMetricsForDpi(SM_CXEDGE, static_cast<UINT>(dpi)) + MulDiv(4, dpi, 96);
    }

    void SidePane::SetTab(int tab)
    {
        if (tab < 0 || tab >= TabCtrl_GetItemCount(tabs)) tab = 0;
        TabCtrl_SetCurSel(tabs, tab);
        ShowPage(tab);
    }

    void SidePane::ShowPage(int tab)
    {
        ShowWindow(explorer->Window(), tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(syncButton, tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(tasks->Window(), tab == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(pagesContent, tab == 2 ? SW_SHOW : SW_HIDE);
        if (searchContent) ShowWindow(searchContent, tab == SidePane::SearchTab ? SW_SHOW : SW_HIDE);
        if (outlineContent) ShowWindow(outlineContent, tab == OutlineTab ? SW_SHOW : SW_HIDE);
    }

    void SidePane::Layout()
    {
        RECT rc;
        GetClientRect(window, &rc);
        MoveWindow(tabs, 0, 0, rc.right, rc.bottom, TRUE);
        RECT page = rc;
        TabCtrl_AdjustRect(tabs, FALSE, &page);
        for (HWND child : { explorer->Window(), tasks->Window(), pagesContent, searchContent, outlineContent })
        {
            if (!child) continue;
            MoveWindow(child, page.left, page.top, page.right - page.left, page.bottom - page.top, TRUE);
            SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); // above the tab control
        }
        // The tree leaves room for the sync button below it.
        int button = MulDiv(SyncButtonHeightDip, static_cast<int>(GetDpiForWindow(window)), 96);
        int gap = button / 4;
        MoveWindow(explorer->Window(), page.left, page.top, page.right - page.left, page.bottom - page.top - button - 2 * gap, TRUE);
        MoveWindow(syncButton, page.left + gap, page.bottom - button - gap, page.right - page.left - 2 * gap, button, TRUE);
        SetWindowPos(syncButton, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    void SidePane::SetSyncing(bool value)
    {
        syncing = value;
        SetWindowTextW(syncButton, syncing ? L"Synchronisiert …" : syncText.c_str());
        EnableWindow(syncButton, !syncing);
    }

    void SidePane::SetSyncStatus(std::wstring text, std::wstring const& details)
    {
        syncText = std::move(text);
        if (!syncing)
        {
            SetWindowTextW(syncButton, syncText.c_str());
        }
        TTTOOLINFOW tool{ sizeof(tool) };
        tool.hwnd = window;
        tool.uId = reinterpret_cast<UINT_PTR>(syncButton);
        std::wstring lines; // the tooltip breaks lines at "\r\n"
        for (wchar_t c : details)
        {
            lines += c == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, c);
        }
        tool.lpszText = lines.data(); // copied by the tooltip
        SendMessageW(syncTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    LRESULT SidePane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_COMMAND:
            if (LOWORD(wp) == SyncId && syncRequested)
            {
                syncRequested();
                return 0;
            }
            break;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wp) == explorer->Window())
            {
                explorer->OnContextMenu(lp);
                return 0;
            }
            break;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_NOTIFY:
        {
            auto const& header = *reinterpret_cast<NMHDR const*>(lp);
            if (header.hwndFrom == tabs && header.code == TCN_SELCHANGE)
            {
                ShowPage(Tab());
                if (tabChanged) tabChanged(Tab());
                return 0;
            }
            if (header.hwndFrom == explorer->Window())
            {
                return explorer->OnNotify(header);
            }
            break;
        }
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK SidePane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<SidePane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->window || !self->explorer || !self->tasks)
        {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("side pane: message 0x{:X}", message));
            return 0;
        }
    }
}
