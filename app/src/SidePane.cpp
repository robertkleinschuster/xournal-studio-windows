#include "SidePane.h"
#include "ChildControls.h"
#include "DarkColors.h"

#include "inkpages/Toolbar.h"

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
        constexpr int FileBarId = 5, SyncLabelId = 6;
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
        CreateFileToolbar();
        tasks = std::make_unique<TasksPane>(window, std::move(messages));
        pagesContent = Placeholder(window, L"Seitenübersicht folgt.");
        ShowPage(0);
        pending.Release();
    }

    void SidePane::CreateFileToolbar()
    {
        fileBar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"Dateiaktionen",
            WS_CHILD | WS_TABSTOP | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS |
            CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(FileBarId)), GetModuleHandleW(nullptr), nullptr);
        if (!fileBar) ThrowLastError("CreateWindowEx (file toolbar)");
        SendMessageW(fileBar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        SendMessageW(fileBar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);

        TBBUTTON buttons[]{
            { 0, SyncId, TBSTATE_ENABLED, BTNS_BUTTON, {}, 0, reinterpret_cast<INT_PTR>(L"Synchronisieren") },
            { 1, CurrentFileId, 0, BTNS_BUTTON, {}, 0, reinterpret_cast<INT_PTR>(L"Ge?ffnete Datei anzeigen") }
        };
        if (!SendMessageW(fileBar, TB_ADDBUTTONSW, std::size(buttons), reinterpret_cast<LPARAM>(buttons)))
            ThrowHr("TB_ADDBUTTONS (file toolbar)", E_FAIL);

        syncTip = reinterpret_cast<HWND>(SendMessageW(fileBar, TB_GETTOOLTIPS, 0, 0));
        if (!syncTip) ThrowHr("TB_GETTOOLTIPS (file toolbar)", E_FAIL);
        syncLabel = CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(SyncLabelId)), GetModuleHandleW(nullptr), nullptr);
        if (!syncLabel) ThrowLastError("CreateWindowEx (sync status)");
        UpdateFileToolbarImages();
    }

    void SidePane::UpdateFileToolbarImages()
    {
        ChildControls::UseMessageFont(syncLabel);
        int dpi = static_cast<int>(GetDpiForWindow(window));
        SendMessageW(syncTip, TTM_SETMAXTIPWIDTH, 0, MulDiv(320, dpi, 96));
        int size = MulDiv(20, dpi, 96);
        std::unique_ptr<std::remove_pointer_t<HIMAGELIST>, decltype(&ImageList_Destroy)> images(
            ImageList_Create(size, size, ILC_COLOR32, 2, 0), ImageList_Destroy);
        if (!images) ThrowHr("ImageList_Create (file toolbar)", E_FAIL);
        for (auto symbol : { inkpages::ToolbarSymbol::Sync, inkpages::ToolbarSymbol::RevealFile })
        {
            HBITMAP bitmap = inkpages::CreateToolbarSymbolBitmap(symbol, size);
            if (!bitmap) ThrowHr("CreateToolbarSymbolBitmap (file toolbar)", E_FAIL);
            int index = ImageList_Add(images.get(), bitmap, nullptr);
            if (!DeleteObject(bitmap)) LogError("DeleteObject (file toolbar bitmap) failed");
            if (index < 0) ThrowHr("ImageList_Add (file toolbar)", E_FAIL);
        }
        int button = MulDiv(32, dpi, 96);
        if (!SendMessageW(fileBar, TB_SETBUTTONSIZE, 0, MAKELPARAM(button, button)))
            ThrowHr("TB_SETBUTTONSIZE (file toolbar)", E_FAIL);
        SendMessageW(fileBar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images.get()));
        fileBarImages = std::move(images);
    }

    SidePane::~SidePane()
    {
        if (IsWindow(fileBar)) SendMessageW(fileBar, TB_SETIMAGELIST, 0, 0);
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
        if (!RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN))
            ThrowHr("RedrawWindow (file toolbar appearance)", E_FAIL);
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
        ShowWindow(fileBar, tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(syncLabel, tab == 0 ? SW_SHOW : SW_HIDE);
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
        SIZE toolbar{};
        if (!SendMessageW(fileBar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&toolbar)))
            ThrowHr("TB_GETMAXSIZE (file toolbar)", E_FAIL);
        LONG barWidth = std::min(width, toolbar.cx);
        LONG barHeight = std::min(height, toolbar.cy);
        LONG statusLeft = std::min(width, barWidth + MulDiv(8, static_cast<int>(GetDpiForWindow(window)), 96));
        if (!MoveWindow(fileBar, page.left, page.top, barWidth, barHeight, TRUE))
            ThrowLastError("MoveWindow (file toolbar)");
        if (!MoveWindow(syncLabel, page.left + statusLeft, page.top, width - statusLeft, barHeight, TRUE))
            ThrowLastError("MoveWindow (sync status)");
        if (!MoveWindow(explorer->Window(), page.left, page.top + barHeight, width, height - barHeight, TRUE))
            ThrowLastError("MoveWindow (explorer tree)");
    }

    void SidePane::SetCurrentFile(std::optional<std::filesystem::path> file)
    {
        if (file == currentFile) return;
        currentFile = std::move(file);
        UpdateCurrentFileButton();
    }

    void SidePane::UpdateCurrentFileButton()
    {
        if (!SendMessageW(fileBar, TB_ENABLEBUTTON, CurrentFileId, MAKELPARAM(currentFile && explorer->HasFile(*currentFile), 0)))
            ThrowHr("TB_ENABLEBUTTON (show open file)", E_FAIL);
    }

    void SidePane::SetSyncing(bool value)
    {
        syncing = value;
        if (!SendMessageW(fileBar, TB_ENABLEBUTTON, SyncId, MAKELPARAM(!syncing, 0)))
            ThrowHr("TB_ENABLEBUTTON (sync)", E_FAIL);
        UpdateSyncLabel();
    }

    void SidePane::UpdateSyncLabel()
    {
        if (!SetWindowTextW(syncLabel, syncing ? L"Synchronisiert ?" : syncText.c_str()))
            ThrowLastError("SetWindowText (sync status)");
    }

    void SidePane::SetSyncStatus(std::wstring text, std::wstring const& details)
    {
        syncText = std::move(text);
        syncDetails = L"Synchronisieren";
        if (!details.empty()) syncDetails += L"\r\n";
        for (wchar_t c : details)
        {
            if (c == L'\n') syncDetails += L'\r';
            syncDetails += c;
        }
        UpdateSyncLabel();
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
        case WM_CTLCOLORSTATIC:
            if (darkMode && reinterpret_cast<HWND>(lp) == syncLabel)
                return reinterpret_cast<LRESULT>(DarkColors::ControlBrush(reinterpret_cast<HDC>(wp), DarkColors::Background));
            break;
        case WM_COMMAND:
            if (LOWORD(wp) == CurrentFileId && reinterpret_cast<HWND>(lp) == fileBar && currentFile)
            {
                explorer->FocusFile(*currentFile);
                return 0;
            }
            if (LOWORD(wp) == SyncId && reinterpret_cast<HWND>(lp) == fileBar && syncRequested)
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
            UpdateFileToolbarImages();
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
            if (darkMode && header.hwndFrom == fileBar && header.code == NM_CUSTOMDRAW)
            {
                auto& draw = *reinterpret_cast<NMTBCUSTOMDRAW*>(lp);
                if (draw.nmcd.dwDrawStage == CDDS_PREPAINT)
                {
                    if (!DarkColors::PaintBackground(fileBar, draw.nmcd.hdc))
                        ThrowHr("FillRect (dark file toolbar)", E_FAIL);
                    return CDRF_NOTIFYITEMDRAW;
                }
                if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    draw.clrHighlightHotTrack = DarkColors::Selected;
                    return TBCDRF_HILITEHOTTRACK;
                }
            }
            if (header.hwndFrom == fileBar && header.code == TBN_GETINFOTIPW)
            {
                auto& tip = *reinterpret_cast<NMTBGETINFOTIPW*>(lp);
                if (tip.iItem == SyncId && tip.cchTextMax > 0 && !syncDetails.empty())
                    wcsncpy_s(tip.pszText, tip.cchTextMax, syncDetails.c_str(), _TRUNCATE);
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
