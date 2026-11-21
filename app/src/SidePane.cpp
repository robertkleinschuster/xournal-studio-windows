#include "SidePane.h"
#include "ChildControls.h"
#include "DarkColors.h"
#include "Dialogs.h"

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
        constexpr int TabsId = 1, TreeId = 2, SyncId = 3;
        constexpr int SyncButtonHeightDip = 32;
        constexpr wchar_t const* TabTitles[] = { L"Dateien", L"Aufgaben", L"Seiten", L"Suche", L"Gliederung" };
        constexpr int OutlineTab = 4, FixedTabs = 4; // Suche is SidePane::SearchTab
        constexpr int FirstTabCommand = 10;

        void AddNavigationButton(HWND bar, int index)
        {
            TBBUTTON button{ I_IMAGENONE, FirstTabCommand + index, TBSTATE_ENABLED,
                BTNS_CHECKGROUP | BTNS_AUTOSIZE | BTNS_SHOWTEXT };
            button.iString = reinterpret_cast<INT_PTR>(TabTitles[index]);
            if (!SendMessageW(bar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&button)))
                ThrowHr("TB_ADDBUTTONS (dark sidebar navigation)", E_FAIL);
        }

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
        navigation = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"Seitenleiste", WS_CHILD | WS_TABSTOP |
            TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN,
            0, 0, 0, 0, window, nullptr, instance, nullptr);
        if (!navigation) ThrowLastError("CreateWindowEx (dark sidebar navigation)");
        ChildControls::UseMessageFont(navigation);
        SendMessageW(navigation, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(navigation, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
        for (int i = 0; i <= OutlineTab; ++i) AddNavigationButton(navigation, i);
        if (!SendMessageW(navigation, TB_HIDEBUTTON, FirstTabCommand + OutlineTab, TRUE))
            ThrowHr("TB_HIDEBUTTON (Gliederung)", E_FAIL);
        if (!SendMessageW(navigation, TB_CHECKBUTTON, FirstTabCommand, TRUE))
            ThrowHr("TB_CHECKBUTTON (Dateien)", E_FAIL);
        explorer = std::make_unique<ExplorerTree>(window, TreeId);
        ChildControls::UseMessageFont(explorer->Window());
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
        if (!SendMessageW(navigation, TB_HIDEBUTTON, FirstTabCommand + OutlineTab, !shown))
            ThrowHr("TB_HIDEBUTTON (Gliederung)", E_FAIL);
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
        ShowWindow(tabs, dark ? SW_HIDE : SW_SHOW);
        ShowWindow(navigation, dark ? SW_SHOW : SW_HIDE);
        Layout();
    }

    int SidePane::TabsWidth(bool includeOutline) const
    {
        SIZE strip{};
        if (darkMode && !SendMessageW(navigation, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&strip)))
            ThrowHr("TB_GETMAXSIZE (sidebar width)", E_FAIL);
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
        return std::max(static_cast<int>(strip.cx),
            width + 2 * GetSystemMetricsForDpi(SM_CXEDGE, static_cast<UINT>(dpi)) + MulDiv(4, dpi, 96));
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
        if (!SendMessageW(navigation, TB_CHECKBUTTON, FirstTabCommand + tab, TRUE))
            ThrowHr("TB_CHECKBUTTON (sidebar navigation)", E_FAIL);
        if (!SetWindowPos(tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            ThrowLastError("SetWindowPos (tab background)");
        ShowWindow(explorer->Window(), tab == 0 ? SW_SHOW : SW_HIDE);
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
        if (darkMode)
        {
            SIZE strip{};
            if (!SendMessageW(navigation, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&strip)))
                ThrowHr("TB_GETMAXSIZE (sidebar navigation)", E_FAIL);
            auto height = std::min(rc.bottom, strip.cy);
            if (!MoveWindow(navigation, 0, 0, rc.right, height, TRUE))
                ThrowLastError("MoveWindow (sidebar navigation)");
            page.top = height + 1;
        }
        if (!MoveWindow(tabs, 0, 0, rc.right, rc.bottom, TRUE)) ThrowLastError("MoveWindow (tabs)");
        LONG width = std::max(0L, page.right - page.left);
        LONG height = std::max(0L, page.bottom - page.top);
        for (HWND child : { tasks->Window(), pagesContent, searchContent, outlineContent })
        {
            if (child && !MoveWindow(child, page.left, page.top, width, height, TRUE))
                ThrowLastError("MoveWindow (side pane content)");
        }
        // The tree leaves room for the sync button below it.
        int button = MulDiv(SyncButtonHeightDip, static_cast<int>(GetDpiForWindow(window)), 96);
        int gap = button / 4;
        if (!MoveWindow(explorer->Window(), page.left, page.top, width, std::max(0L, height - button - 2 * gap), TRUE))
            ThrowLastError("MoveWindow (explorer tree)");
        if (!MoveWindow(syncButton, page.left + gap, page.bottom - button - gap, std::max(0L, width - 2 * gap), button, TRUE))
            ThrowLastError("MoveWindow (sync button)");
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
            if (reinterpret_cast<HWND>(lp) == navigation &&
                LOWORD(wp) >= FirstTabCommand && LOWORD(wp) <= FirstTabCommand + OutlineTab)
            {
                int previous = Tab();
                SetTab(LOWORD(wp) - FirstTabCommand);
                if (Tab() != previous && tabChanged) tabChanged(Tab());
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
            ChildControls::UseMessageFont(navigation);
            ChildControls::UseMessageFont(explorer->Window());
            ChildControls::UseMessageFont(syncButton);
            SendMessageW(syncTip, TTM_SETMAXTIPWIDTH, 0, MulDiv(320, static_cast<int>(GetDpiForWindow(window)), 96));
            Layout();
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_NOTIFY:
        {
            auto const& header = *reinterpret_cast<NMHDR const*>(lp);
            if (darkMode && header.hwndFrom == navigation && header.code == NM_CUSTOMDRAW)
            {
                auto& draw = *reinterpret_cast<NMTBCUSTOMDRAW*>(lp);
                if (draw.nmcd.dwDrawStage == CDDS_PREPAINT)
                {
                    if (!DarkColors::PaintBackground(navigation, draw.nmcd.hdc))
                        ThrowHr("FillRect (sidebar navigation)", E_FAIL);
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    auto state = draw.nmcd.uItemState;
                    if (state & (CDIS_CHECKED | CDIS_SELECTED | CDIS_HOT))
                    {
                        auto color = state & (CDIS_CHECKED | CDIS_SELECTED) ? DarkColors::Selected : DarkColors::Input;
                        if (!FillRect(draw.nmcd.hdc, &draw.nmcd.rc, DarkColors::ControlBrush(draw.nmcd.hdc, color)))
                            ThrowHr("FillRect (sidebar navigation button)", E_FAIL);
                    }
                    draw.clrText = DarkColors::Text;
                    draw.clrTextHighlight = DarkColors::Text;
                    return TBCDRF_USECDCOLORS | TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES;
                }
            }
            if (header.hwndFrom == syncButton &&
                GetPropW(GetAncestor(window, GA_ROOT), DarkColors::WindowProperty) && Dialogs::DrawDarkPushButton(header))
                return CDRF_SKIPDEFAULT;
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
