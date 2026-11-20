#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <format>
#include <map>

namespace xournal::app::ChildControls
{
    bool SetWindowData(HWND window, void* owner) noexcept
    {
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner))) return true;
        DWORD error = GetLastError();
        if (error == ERROR_SUCCESS) return true;
        try
        {
            LogError(std::format("SetWindowLongPtr (window owner): {}", DescribeHr(HRESULT_FROM_WIN32(error))));
        }
        catch (...)
        {
            OutputDebugStringW(L"SetWindowLongPtr failed while setting the window owner.\n");
        }
        SetLastError(error);
        return false;
    }

    PendingPaneWindow::~PendingPaneWindow()
    {
        if (window && !DestroyWindow(window))
            LogError(std::format("DestroyWindow (incomplete pane): {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
    }

    BatchRedraw::BatchRedraw(HWND window)
    {
        if (IsWindowVisible(window))
        {
            control = window;
            SendMessageW(control, WM_SETREDRAW, FALSE, 0);
        }
    }

    BatchRedraw::~BatchRedraw()
    {
        if (!control) return;
        SendMessageW(control, WM_SETREDRAW, TRUE, 0);
        if (!RedrawWindow(control, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN))
            LogError("RedrawWindow (batched control update) failed");
    }

    namespace
    {
        struct MessageFont
        {
            HFONT handle{};
            ~MessageFont()
            {
                if (handle && !DeleteObject(handle)) LogError("DeleteObject (message font) failed");
            }
        };
    }

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
        UINT dpi = GetDpiForWindow(control);
        if (!dpi)
        {
            LogError("GetDpiForWindow (message font) returned zero");
            return;
        }
        static std::map<UINT, MessageFont> fonts;
        auto& font = fonts[dpi];
        if (!font.handle)
        {
            NONCLIENTMETRICSW metrics{ sizeof(metrics) };
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
            {
                LogError(std::format("message font: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                return;
            }
            font.handle = CreateFontIndirectW(&metrics.lfMessageFont);
            if (!font.handle)
            {
                LogError("CreateFontIndirectW (message font) failed");
                return;
            }
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font.handle), TRUE);
    }
}
