#include "SidePane.h"
#include "ChildControls.h"
#include "DarkColors.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>

#include <algorithm>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioSidePane";
        constexpr int TabsId = 1, TreeId = 2, SyncId = 3, CurrentFileId = 4;
        constexpr int FileButtonHeightDip = 32;
        constexpr wchar_t const* TabTitles[] = { L"Dateien", L"Aufgaben", L"Seiten", L"Suche", L"Gliederung" };
        constexpr int OutlineTab = 4, FixedTabs = 4; // Suche is SidePane::SearchTab
        void InsertTab(HWND tabs, int index)
        {
            TCITEMW item{ TCIF_TEXT };
            item.pszText = const_cast<wchar_t*>(TabTitles[index]);
            if (TabCtrl_InsertItem(tabs, index, &item) == -1)
                ThrowHr("TCM_INSERTITEM (side pane)", E_FAIL);
        }

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
        ChildControls::PendingPaneWindow pending(window);
        tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(TabsId)), instance, nullptr);
        if (!tabs)
        {
            ThrowLastError("CreateWindowEx(tab control)");
        }
        ChildControls::UseMessageFont(tabs);
        for (int i = 0; i < FixedTabs; ++i)
            InsertTab(tabs, i);
        explorer = std::make_unique<ExplorerTree>(window, TreeId);
        ChildControls::UseMessageFont(explorer->Window());
        currentFileButton = CreateWindowExW(0, WC_BUTTONW, L"Geöffnete Datei anzeigen",
            WS_CHILD | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(CurrentFileId)), instance, nullptr);
        if (!currentFileButton) ThrowLastError("CreateWindowEx (show open file button)");
        ChildControls::UseMessageFont(currentFileButton);
        syncButton = CreateWindowExW(0, WC_BUTTONW, L"Synchronisieren", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SyncId)), instance, nullptr);
        if (!syncButton)
        {
            ThrowLastError("CreateWindowEx(sync button)");
        }
        ChildControls::UseMessageFont(syncButton);
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
            ThrowHr("TTM_ADDTOOL(sync button)", E_FAIL);
        }
        SendMessageW(syncTip, TTM_SETMAXTIPWIDTH, 0, MulDiv(320, static_cast<int>(GetDpiForWindow(window)), 96)); // lines
        tasks = std::make_unique<TasksPane>(window, std::move(messages));
        pagesContent = Placeholder(window, L"Seitenübersicht folgt.");
        ShowPage(0);
        pending.Release();
    }

    SidePane::~SidePane()
    {
        if (window && IsWindow(window))
        {
            ChildControls::SetWindowData(window, nullptr);
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
        if (shown == (TabCtrl_GetItemCount(tabs) > OutlineTab)) return;
        if (shown)
        {
            InsertTab(tabs, OutlineTab);
            Layout(); // a new row of tabs may change the content's area
            return;
        }
        bool wasShown = Tab() == OutlineTab;
        if (!TabCtrl_DeleteItem(tabs, OutlineTab)) ThrowHr("TCM_DELETEITEM (Gliederung)", E_FAIL);
        if (wasShown)
        {
            SetTab(0);
            if (tabChanged) tabChanged(0);
        }
        Layout();
    }

    int SidePane::Tab() const { return TabCtrl_GetCurSel(tabs); }

    void SidePane::SetDarkMode(bool dark)
    {
        if (darkMode == dark) return;
        darkMode = dark;
        DarkColors::SetTooltipPalette(syncTip, dark);
    }

    int SidePane::TabsWidth(bool includeOutline) const
    {
        int count = TabCtrl_GetItemCount(tabs);
        RECT first{}, last{};
        if (count <= 0 || !TabCtrl_GetItemRect(tabs, 0, &first) || !TabCtrl_GetItemRect(tabs, count - 1, &last))
        {
            ThrowHr("TCM_GETITEMRECT (side pane tabs)", E_FAIL);
        }
        int width = last.right - first.left; // independent of how far the strip is scrolled
        if (includeOutline && count == FixedTabs)
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
        if (Tab() != tab) ThrowHr("TCM_SETCURSEL (side pane)", E_FAIL);
        ShowPage(tab);
    }

    void SidePane::ShowPage(int tab)
    {
        if (!SetWindowPos(tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            ThrowLastError("SetWindowPos (tab background)");
        ShowWindow(explorer->Window(), tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(currentFileButton, tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(syncButton, tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(tasks->Window(), tab == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(pagesContent, tab == 2 ? SW_SHOW : SW_HIDE);
        if (searchContent) ShowWindow(searchContent, tab == SidePane::SearchTab ? SW_SHOW : SW_HIDE);
        if (outlineContent) ShowWindow(outlineContent, tab == OutlineTab ? SW_SHOW : SW_HIDE);
    }

    void SidePane::Layout()
    {
        RECT rc{};
        if (!GetClientRect(window, &rc)) ThrowLastError("GetClientRect (side pane)");
        RECT page = rc;
        TabCtrl_AdjustRect(tabs, FALSE, &page);
        if (!MoveWindow(tabs, 0, 0, rc.right, rc.bottom, TRUE)) ThrowLastError("MoveWindow (tabs)");
        LONG width = std::max(0L, page.right - page.left);
        LONG height = std::max(0L, page.bottom - page.top);
        for (HWND child : { tasks->Window(), pagesContent, searchContent, outlineContent })
        {
            if (child && !MoveWindow(child, page.left, page.top, width, height, TRUE))
                ThrowLastError("MoveWindow (side pane content)");
        }
        // Show the open file above the tree and keep sync below it.
        int button = MulDiv(FileButtonHeightDip, static_cast<int>(GetDpiForWindow(window)), 96);
        int gap = button / 4;
        if (!MoveWindow(currentFileButton, page.left + gap, page.top + gap, std::max(0L, width - 2 * gap), button, TRUE))
            ThrowLastError("MoveWindow (show open file button)");
        if (!MoveWindow(explorer->Window(), page.left, page.top + button + 2 * gap,
            width, std::max(0L, height - 2 * button - 4 * gap), TRUE))
            ThrowLastError("MoveWindow (explorer tree)");
        if (!MoveWindow(syncButton, page.left + gap, page.bottom - button - gap, std::max(0L, width - 2 * gap), button, TRUE))
            ThrowLastError("MoveWindow (sync button)");
    }

    void SidePane::SetCurrentFile(std::optional<std::filesystem::path> file)
    {
        if (file == currentFile) return;
        currentFile = std::move(file);
        UpdateCurrentFileButton();
    }

    void SidePane::UpdateCurrentFileButton()
    {
        EnableWindow(currentFileButton, currentFile && explorer->HasFile(*currentFile));
    }

    void SidePane::SetSyncing(bool value)
    {
        if (!SetWindowTextW(syncButton, value ? L"Synchronisiert …" : syncText.c_str()))
            ThrowLastError("SetWindowText (sync button)");
        syncing = value;
        EnableWindow(syncButton, !syncing);
    }

    void SidePane::SetSyncStatus(std::wstring text, std::wstring const& details)
    {
        syncText = std::move(text);
        if (!syncing && !SetWindowTextW(syncButton, syncText.c_str()))
            ThrowLastError("SetWindowText (sync button)");
        TTTOOLINFOW tool{ sizeof(tool) };
        tool.hwnd = window;
        tool.uId = reinterpret_cast<UINT_PTR>(syncButton);
        std::wstring lines; // the tooltip breaks lines at "\r\n"
        lines.reserve(details.size());
        for (wchar_t c : details)
        {
            if (c == L'\n') lines += L'\r';
            lines += c;
        }
        tool.lpszText = lines.data(); // copied by the tooltip
        SendMessageW(syncTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    LRESULT SidePane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            if (darkMode)
            {
                RECT area{};
                if (!GetClientRect(window, &area)) ThrowLastError("GetClientRect (side pane background)");
                auto dc = reinterpret_cast<HDC>(wp);
                SetDCBrushColor(dc, DarkColors::Background);
                if (!FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))))
                    ThrowHr("FillRect (side pane background)", E_FAIL);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == CurrentFileId && HIWORD(wp) == BN_CLICKED && currentFile)
            {
                explorer->FocusFile(*currentFile);
                return 0;
            }
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
        case WM_DPICHANGED_AFTERPARENT:
            ChildControls::UseMessageFont(tabs);
            ChildControls::UseMessageFont(explorer->Window());
            ChildControls::UseMessageFont(syncButton);
            ChildControls::UseMessageFont(currentFileButton);
            SendMessageW(syncTip, TTM_SETMAXTIPWIDTH, 0, MulDiv(320, static_cast<int>(GetDpiForWindow(window)), 96));
            Layout();
            return 0;
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
            if (!ChildControls::SetWindowData(hwnd, reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)) return FALSE;
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
