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
        constexpr int TabsId = 1, TreeId = 2;
        constexpr wchar_t const* TabTitles[] = { L"Dateien", L"Aufgaben", L"Seiten" };

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

    SidePane::SidePane(HWND parent)
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
        for (int i = 0; i < 3; ++i)
        {
            TCITEMW item{ TCIF_TEXT };
            item.pszText = const_cast<wchar_t*>(TabTitles[i]);
            TabCtrl_InsertItem(tabs, i, &item);
        }
        explorer = std::make_unique<ExplorerTree>(window, TreeId);
        UseMessageFont(explorer->Window());
        tasksPlaceholder = Placeholder(window, L"Aufgaben (todo.txt) folgen.");
        pagesPlaceholder = Placeholder(window, L"Seitenübersicht folgt.");
        UseMessageFont(tasksPlaceholder);
        UseMessageFont(pagesPlaceholder);
        ShowPage(0);
    }

    SidePane::~SidePane()
    {
        if (window && IsWindow(window))
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    }

    int SidePane::Tab() const { return TabCtrl_GetCurSel(tabs); }

    void SidePane::SetTab(int tab)
    {
        if (tab < 0 || tab > 2) tab = 0;
        TabCtrl_SetCurSel(tabs, tab);
        ShowPage(tab);
    }

    void SidePane::ShowPage(int tab)
    {
        ShowWindow(explorer->Window(), tab == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(tasksPlaceholder, tab == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(pagesPlaceholder, tab == 2 ? SW_SHOW : SW_HIDE);
    }

    void SidePane::Layout()
    {
        RECT rc;
        GetClientRect(window, &rc);
        MoveWindow(tabs, 0, 0, rc.right, rc.bottom, TRUE);
        RECT page = rc;
        TabCtrl_AdjustRect(tabs, FALSE, &page);
        for (HWND child : { explorer->Window(), tasksPlaceholder, pagesPlaceholder })
        {
            MoveWindow(child, page.left, page.top, page.right - page.left, page.bottom - page.top, TRUE);
            SetWindowPos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); // above the tab control
        }
    }

    LRESULT SidePane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
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
        if (!self || !self->window || !self->explorer)
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
