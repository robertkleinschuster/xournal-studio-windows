#include "LinkHover.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr UINT_PTR ToolId = 1;
        constexpr int MaxWidthDip = 480; // long web addresses wrap

        TTTOOLINFOW Tool(HWND owner, wchar_t const* text)
        {
            TTTOOLINFOW tool{ sizeof(tool) };
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = owner;
            tool.uId = ToolId;
            tool.lpszText = const_cast<wchar_t*>(text);
            return tool;
        }
    }

    LinkHover::LinkHover(HWND owner, PdfLinks& links) : owner(owner), links(links)
    {
        tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!tip) ThrowLastError("CreateWindowEx(link tooltip)");
        TTTOOLINFOW tool = Tool(owner, L"");
        if (!SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) ThrowLastError("TTM_ADDTOOL(link tooltip)");
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, MulDiv(MaxWidthDip, static_cast<int>(GetDpiForWindow(owner)), 96));
    }

    LinkHover::~LinkHover()
    {
        if (tip) DestroyWindow(tip);
    }

    LPCWSTR LinkHover::CursorAt(int page, inkpages::PointF point)
    {
        auto link = page >= 0 ? links.LinkAt(page, point) : std::nullopt;
        if (!link)
        {
            Hide();
            return nullptr;
        }
        Show(links.Describe(*link));
        return IDC_HAND;
    }

    void LinkHover::Show(std::wstring const& shown)
    {
        POINT at{};
        if (!GetCursorPos(&at))
        {
            LogError(std::format("link tooltip: GetCursorPos: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            return;
        }
        // Below the pointer (the cursor's height), so the tooltip never lies under the mouse.
        int below = GetSystemMetricsForDpi(SM_CYCURSOR, GetDpiForWindow(owner)) * 2 / 3;
        SendMessageW(tip, TTM_TRACKPOSITION, 0, MAKELPARAM(at.x, at.y + below));
        if (shown == text) return;
        text = shown;
        Log(std::format("link tooltip: {}", ToUtf8(text)));
        TTTOOLINFOW tool = Tool(owner, text.c_str());
        SendMessageW(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    }

    void LinkHover::Hide()
    {
        if (text.empty()) return;
        text.clear();
        TTTOOLINFOW tool = Tool(owner, L"");
        SendMessageW(tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
}
