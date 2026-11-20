#include "StatusBar.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>

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
        if (!SendMessageW(bar, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str())))
        {
            LogError("SB_SETTEXTW (status bar) failed");
            return;
        }
        shown = text;
        Log(std::format("status: {}", ToUtf8(shown)));
    }

    void StatusBar::ShowMessage(std::wstring const& text, MessageKind kind)
    {
        char const* kinds[] = { "success", "info", "warning", "error" };
        Log(std::format("status message ({}): {}", kinds[static_cast<int>(kind)], ToUtf8(text)));
        std::wstring shown = kind == MessageKind::Error ? L"Fehler: " + text :
            kind == MessageKind::Warning ? L"Warnung: " + text : text;
        if (!SendMessageW(bar, SB_SETTEXTW, MessagePart | SBT_NOTABPARSING, reinterpret_cast<LPARAM>(shown.c_str())))
            LogError("SB_SETTEXTW (status message) failed");
        SendMessageW(bar, SB_SETTIPTEXTW, MessagePart, reinterpret_cast<LPARAM>(shown.c_str()));
    }

    bool StatusBar::OnNotify(NMHDR const& header)
    {
        if (header.hwndFrom != bar || header.code != NM_DBLCLK) return false;
        auto const& mouse = reinterpret_cast<NMMOUSE const&>(header);
        if (mouse.dwItemSpec == 0 && pageDoubleClicked) pageDoubleClicked();
        return true;
    }
}
