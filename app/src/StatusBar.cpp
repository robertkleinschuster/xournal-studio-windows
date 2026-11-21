#include <initguid.h>
#include "StatusBar.h"
#include "DarkColors.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr int PagePartDip = 140, ZoomPartDip = 70, MessagePart = 2;
    }

    StatusBar::StatusBar(HWND parent)
    {
        bar = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_TOOLTIPS, 0, 0, 0, 0, parent, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!bar) ThrowLastError("CreateWindowEx(status bar)");
        Layout();
    }

    StatusBar::~StatusBar()
    {
        ClearAccessibleNames();
    }

    void StatusBar::ClearAccessibleNames()
    {
        if (!accessibleNames || !IsWindow(bar)) return;
        for (int part = 0; part <= MessagePart; ++part)
        {
            HRESULT hr = accessibleNames->ClearHwndProps(bar, static_cast<DWORD>(OBJID_CLIENT), part + 1, &PROPID_ACC_NAME, 1);
            if (FAILED(hr)) LogError("ClearHwndProps (status bar): " + DescribeHr(hr));
        }
        accessibleNames.Reset();
    }

    int StatusBar::Height() const
    {
        RECT rc{};
        if (!GetWindowRect(bar, &rc)) ThrowLastError("GetWindowRect (status bar)");
        return rc.bottom - rc.top;
    }

    void StatusBar::Layout()
    {
        SendMessageW(bar, WM_SIZE, 0, 0); // the bar sizes itself to the parent's bottom
        UINT windowDpi = GetDpiForWindow(bar);
        if (!windowDpi || partsDpi == windowDpi) return;
        int dpi = static_cast<int>(windowDpi);
        int page = MulDiv(PagePartDip, dpi, 96);
        int parts[] = { page, page + MulDiv(ZoomPartDip, dpi, 96), -1 };
        if (!SendMessageW(bar, SB_SETPARTS, ARRAYSIZE(parts), reinterpret_cast<LPARAM>(parts)))
            ThrowHr("SB_SETPARTS (status bar)", E_FAIL);
        partsDpi = windowDpi;
    }

    void StatusBar::Show(int page, int count, int percent)
    {
        SetPart(0, page >= 0 && count > 0 ? std::format(L"Seite {} von {}", page + 1, count) : L"", pageText);
        SetPart(1, std::format(L"{} %", percent), zoomText);
    }

    void StatusBar::SetPart(int part, std::wstring const& text, std::wstring& shown)
    {
        if (text == shown) return; // the view moves often: only changes are drawn
        shown = text;
        UpdatePart(part, shown);
        Log(std::format("status: {}", ToUtf8(shown)));
    }

    void StatusBar::ShowMessage(std::wstring const& text, MessageKind kind)
    {
        char const* kinds[] = { "success", "info", "warning", "error" };
        Log(std::format("status message ({}): {}", kinds[static_cast<int>(kind)], ToUtf8(text)));
        messageText = kind == MessageKind::Error ? L"Fehler: " + text :
            kind == MessageKind::Warning ? L"Warnung: " + text : text;
        UpdatePart(MessagePart, messageText);
        SendMessageW(bar, SB_SETTIPTEXTW, MessagePart, reinterpret_cast<LPARAM>(messageText.c_str()));
    }

    void StatusBar::UpdatePart(int part, std::wstring const& text)
    {
        WPARAM style = darkMode ? SBT_OWNERDRAW | SBT_NOBORDERS : SBT_NOTABPARSING;
        LPARAM data = darkMode ? part : reinterpret_cast<LPARAM>(text.c_str());
        if (!SendMessageW(bar, SB_SETTEXTW, part | style, data))
            LogError("SB_SETTEXTW (status bar) failed");
        if (accessibleNames)
        {
            DWORD child = part + 1; // MSAA child IDs are one-based.
            HRESULT hr = accessibleNames->SetHwndPropStr(bar, static_cast<DWORD>(OBJID_CLIENT), child, PROPID_ACC_NAME, text.c_str());
            if (FAILED(hr)) LogError("SetHwndPropStr (status bar): " + DescribeHr(hr));
            else NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, bar, OBJID_CLIENT, child);
        }
    }

    void StatusBar::SetDarkMode(bool dark)
    {
        if (dark && !accessibleNames)
        {
            HRESULT hr = CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(accessibleNames.GetAddressOf()));
            if (FAILED(hr)) LogError("CoCreateInstance (status bar accessibility): " + DescribeHr(hr));
        }
        else if (!dark) ClearAccessibleNames();
        darkMode = dark;
        HRESULT hr = SetWindowTheme(bar, dark ? L"" : nullptr, dark ? L"" : nullptr);
        if (FAILED(hr)) LogError("SetWindowTheme (status bar): " + DescribeHr(hr));
        SendMessageW(bar, SB_SETBKCOLOR, 0, dark ? DarkColors::Background : CLR_DEFAULT);
        UpdatePart(0, pageText);
        UpdatePart(1, zoomText);
        UpdatePart(MessagePart, messageText);
    }

    bool StatusBar::DrawPart(DRAWITEMSTRUCT const& item) const
    {
        if (item.hwndItem != bar || item.itemID > MessagePart) return false;
        auto const& text = item.itemID == 0 ? pageText : item.itemID == 1 ? zoomText : messageText;
        auto dc = item.hDC;
        SetDCBrushColor(dc, DarkColors::Background);
        FillRect(dc, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, DarkColors::Text);
        RECT rect = item.rcItem;
        rect.left += MulDiv(6, GetDpiForWindow(bar), 96);
        DrawTextW(dc, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        return true;
    }

    bool StatusBar::OnNotify(NMHDR const& header)
    {
        if (header.hwndFrom != bar || header.code != NM_DBLCLK) return false;
        auto const& mouse = reinterpret_cast<NMMOUSE const&>(header);
        if (mouse.dwItemSpec == 0 && pageDoubleClicked) pageDoubleClicked();
        return true;
    }
}
