#include "StatusBar.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>

#include <algorithm>
#include <format>
#include <utility>

namespace xournal::app
{
    namespace
    {
        constexpr int GitPart = 0, MessagePart = 1, PagePart = 2, ZoomPart = 3;
        constexpr int GitPartDip = 190, PagePartDip = 120, ZoomPartDip = 60;
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
        RECT client{};
        if (!GetClientRect(bar, &client)) ThrowLastError("GetClientRect (status bar)");
        int width = client.right;
        if (!windowDpi || (partsDpi == windowDpi && partsWidth == width)) return;
        int dpi = static_cast<int>(windowDpi);
        // The git status stands at the left, the message takes what is left, the page and zoom end at the right.
        int zoom = MulDiv(ZoomPartDip, dpi, 96), page = MulDiv(PagePartDip, dpi, 96), git = MulDiv(GitPartDip, dpi, 96);
        int zoomStart = std::max(git, width - zoom);
        int pageStart = std::max(git, zoomStart - page);
        int parts[4]{};
        parts[GitPart] = git;
        parts[MessagePart] = pageStart; // a part's value is its right edge
        parts[PagePart] = zoomStart;
        parts[ZoomPart] = -1;
        if (!SendMessageW(bar, SB_SETPARTS, ARRAYSIZE(parts), reinterpret_cast<LPARAM>(parts)))
            ThrowHr("SB_SETPARTS (status bar)", E_FAIL);
        partsDpi = windowDpi;
        partsWidth = width;
        for (auto const& [part, text] : { std::pair(GitPart, &gitText), std::pair(MessagePart, &messageText),
                                           std::pair(PagePart, &pageText), std::pair(ZoomPart, &zoomText) })
        {
            if (!text->empty()) UpdatePart(part, *text); // the parts were set anew: draw what they show
        }
    }

    void StatusBar::Show(int page, int count, int percent)
    {
        SetPart(PagePart, page >= 0 && count > 0 ? std::format(L"Seite {} von {}", page + 1, count) : L"", pageText);
        SetPart(ZoomPart, std::format(L"{} %", percent), zoomText);
    }

    void StatusBar::ShowGit(std::wstring const& text, std::wstring const& details)
    {
        SetPart(GitPart, text, gitText);
        gitTip = details; // kept: the control may refer to the text instead of copying it
        SendMessageW(bar, SB_SETTIPTEXTW, GitPart, reinterpret_cast<LPARAM>(gitTip.c_str()));
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
        if (!SendMessageW(bar, SB_SETTEXTW, part | SBT_NOTABPARSING, reinterpret_cast<LPARAM>(text.c_str())))
            LogError("SB_SETTEXTW (status bar) failed");
    }

    bool StatusBar::OnNotify(NMHDR const& header)
    {
        if (header.hwndFrom != bar || header.code != NM_DBLCLK) return false;
        auto const& mouse = reinterpret_cast<NMMOUSE const&>(header);
        if (mouse.dwItemSpec == PagePart && pageDoubleClicked) pageDoubleClicked();
        return true;
    }
}
