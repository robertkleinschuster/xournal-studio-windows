#include "TabStrip.h"

#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <windowsx.h>

#include <algorithm>

namespace xournal::app
{
    namespace
    {
        constexpr UINT_PTR SubclassId = 1;
        constexpr UINT CloseCommand = 1, CloseOthersCommand = 2;

        std::wstring Label(TabEntry const& tab)
        {
            std::wstring text = tab.modified ? L"• " + tab.name : tab.name;
            std::wstring escaped; // the control reads '&' as an access key
            for (wchar_t c : text)
            {
                escaped += c;
                if (c == L'&') escaped += L'&';
            }
            return escaped;
        }
    }

    TabStrip::TabStrip(HWND parent)
    {
        strip = ChildControls::Create(parent, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS | TCS_TOOLTIPS | TCS_FOCUSNEVER, 0);
        ChildControls::UseMessageFont(strip);
        if (!SetWindowSubclass(strip, SubclassProc, SubclassId, reinterpret_cast<DWORD_PTR>(this)))
            ThrowLastError("SetWindowSubclass (tab strip)");
    }

    TabStrip::~TabStrip()
    {
        if (strip && IsWindow(strip) && !RemoveWindowSubclass(strip, SubclassProc, SubclassId))
            LogError("RemoveWindowSubclass (tab strip)");
    }

    int TabStrip::Height() const
    {
        RECT rc{ 0, 0, 100, 0 };
        TabCtrl_AdjustRect(strip, TRUE, &rc); // the display area of an empty rectangle: its height is the strip's
        return rc.bottom - rc.top;
    }

    void TabStrip::Set(std::vector<TabEntry> const& tabs, int active)
    {
        bool same = tabs.size() == shown.size();
        for (size_t i = 0; same && i < tabs.size(); ++i)
            same = tabs[i].name == shown[i].name && tabs[i].modified == shown[i].modified && tabs[i].tip == shown[i].tip;
        if (!same)
        {
            ChildControls::BatchRedraw batch(strip);
            if (tabs.size() != shown.size())
            {
                if (!TabCtrl_DeleteAllItems(strip)) ThrowHr("TCM_DELETEALLITEMS", E_FAIL);
                for (size_t i = 0; i < tabs.size(); ++i)
                {
                    TCITEMW item{ TCIF_TEXT };
                    item.pszText = const_cast<LPWSTR>(L"");
                    if (TabCtrl_InsertItem(strip, static_cast<int>(i), &item) == -1) ThrowHr("TCM_INSERTITEM", E_FAIL);
                }
            }
            for (size_t i = 0; i < tabs.size(); ++i)
            {
                std::wstring label = Label(tabs[i]);
                TCITEMW item{ TCIF_TEXT };
                item.pszText = label.data();
                if (!TabCtrl_SetItem(strip, static_cast<int>(i), &item)) ThrowHr("TCM_SETITEM", E_FAIL);
            }
            shown = tabs;
        }
        if (TabCtrl_GetCurSel(strip) != active) TabCtrl_SetCurSel(strip, active);
    }

    bool TabStrip::OnNotify(NMHDR const& header, LRESULT& result)
    {
        if (header.hwndFrom != strip && header.hwndFrom != reinterpret_cast<HWND>(TabCtrl_GetToolTips(strip))) return false;
        result = 0;
        switch (header.code)
        {
        case TCN_SELCHANGE:
            if (chosen) chosen(TabCtrl_GetCurSel(strip));
            return true;
        case NM_RCLICK:
        {
            POINT p;
            GetCursorPos(&p);
            TCHITTESTINFO hit{ p, 0 };
            ScreenToClient(strip, &hit.pt);
            int tab = TabCtrl_HitTest(strip, &hit);
            if (tab >= 0) ShowMenu(tab);
            return true;
        }
        case TTN_GETDISPINFOW:
        {
            auto& info = *reinterpret_cast<NMTTDISPINFOW*>(const_cast<NMHDR*>(&header));
            size_t tab = static_cast<size_t>(info.hdr.idFrom);
            if (tab < shown.size())
            {
                tipText = shown[tab].tip.empty() ? shown[tab].name : shown[tab].tip;
                info.lpszText = tipText.data();
            }
            return true;
        }
        }
        return false;
    }

    void TabStrip::ShowMenu(int tab)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu) ThrowLastError("CreatePopupMenu (tab)");
        AppendMenuW(menu, MF_STRING, CloseCommand, L"&Schließen");
        AppendMenuW(menu, MF_STRING | (shown.size() > 1 ? MF_ENABLED : MF_GRAYED), CloseOthersCommand,
            L"&Andere schließen");
        POINT p;
        GetCursorPos(&p);
        UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, GetParent(strip), nullptr));
        if (!DestroyMenu(menu)) LogError("DestroyMenu (tab menu)");
        // After the menu is gone: closing may open a question.
        if (command == CloseCommand && closeRequested) closeRequested(tab);
        if (command == CloseOthersCommand && closeOthersRequested) closeOthersRequested(tab);
    }

    LRESULT CALLBACK TabStrip::SubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
    {
        auto* self = reinterpret_cast<TabStrip*>(data);
        if (message == WM_MBUTTONUP)
        {
            // A middle click closes the tab under the pointer, as in browsers and IDEs.
            TCHITTESTINFO hit{ { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, 0 };
            int tab = TabCtrl_HitTest(hwnd, &hit);
            if (tab >= 0 && self->closeRequested) self->closeRequested(tab);
            return 0;
        }
        if (message == WM_DPICHANGED_AFTERPARENT)
        {
            LRESULT result = DefSubclassProc(hwnd, message, wp, lp);
            ChildControls::UseMessageFont(hwnd);
            if (self->dpiChanged) self->dpiChanged();
            return result;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, SubclassProc, SubclassId);
        return DefSubclassProc(hwnd, message, wp, lp);
    }
}
