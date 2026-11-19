#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <format>

namespace xournal::app::ChildControls
{
    HWND Create(HWND parent, wchar_t const* type, wchar_t const* text, DWORD style, int id, DWORD exStyle)
    {
        HWND control = CreateWindowExW(exStyle, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!control)
        {
            ThrowLastError(std::format("CreateWindowEx(control {})", id));
        }
        UseMessageFont(control);
        return control;
    }

    void UseMessageFont(HWND control)
    {
        NONCLIENTMETRICSW metrics{ sizeof(metrics) };
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, GetDpiForWindow(control)))
        {
            LogError(std::format("message font: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            return;
        }
        static HFONT font = CreateFontIndirectW(&metrics.lfMessageFont); // one per process, kept until exit
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}
