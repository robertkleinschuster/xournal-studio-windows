#include "Toast.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <dwmapi.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioToast";
        constexpr UINT_PTR CloseTimer = 1;
        constexpr UINT SuccessMs = 5000;

        int Scaled(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), 96); }

        COLORREF Background(ToastKind kind)
        {
            switch (kind)
            {
            case ToastKind::Success: return RGB(0x0F, 0x7B, 0x0F);
            case ToastKind::Error: return RGB(0xC4, 0x2B, 0x1C);
            case ToastKind::Warning: return RGB(0x9D, 0x5D, 0x00);
            default: return RGB(0x2B, 0x2B, 0x2B);
            }
        }
    }

    Toast::Toast(HWND owner) : owner(owner)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        if (!GetClassInfoExW(instance, ClassName, &wc))
        {
            wc = { sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
            wc.lpszClassName = ClassName;
            if (!RegisterClassExW(&wc))
            {
                ThrowLastError("RegisterClassEx(toast)");
            }
        }
        window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ClassName, L"", WS_POPUP, 0, 0, 0, 0,
            owner, nullptr, instance, this);
        if (!window)
        {
            ThrowLastError("CreateWindowEx(toast)");
        }
        if (!SetLayeredWindowAttributes(window, 0, 245, LWA_ALPHA))
        {
            ThrowLastError("SetLayeredWindowAttributes(toast)");
        }
        DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND; // Windows 11's rounded corners
        HRESULT hr = DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        if (FAILED(hr))
        {
            LogError(std::format("toast: rounded corners not set: {}", DescribeHr(hr)));
        }
    }

    Toast::~Toast()
    {
        if (window)
        {
            DestroyWindow(window);
        }
        if (font)
        {
            DeleteObject(font);
        }
    }

    void Toast::Show(std::wstring message, ToastKind messageKind)
    {
        text = std::move(message);
        kind = messageKind;
        char const* kinds[] = { "success", "info", "warning", "error" };
        Log(std::format("toast ({}): {}", kinds[static_cast<int>(kind)], ToUtf8(text)));
        KillTimer(window, CloseTimer);
        if (kind == ToastKind::Success || kind == ToastKind::Info)
        {
            SetTimer(window, CloseTimer, SuccessMs, nullptr);
        }
        Layout();
        ShowWindow(window, SW_SHOWNOACTIVATE);
        InvalidateRect(window, nullptr, TRUE);
    }

    void Toast::Place(RECT surface)
    {
        area = surface;
        if (IsWindowVisible(window))
        {
            Layout();
        }
    }

    HFONT Toast::Font()
    {
        UINT dpi = GetDpiForWindow(owner);
        if (!font || fontDpi != dpi)
        {
            NONCLIENTMETRICSW metrics{ sizeof(metrics) };
            if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi))
            {
                ThrowLastError("SystemParametersInfoForDpi(NONCLIENTMETRICS)");
            }
            if (font)
            {
                DeleteObject(font);
            }
            font = CreateFontIndirectW(&metrics.lfMessageFont);
            fontDpi = dpi;
        }
        return font;
    }

    void Toast::Layout()
    {
        UINT dpi = GetDpiForWindow(owner);
        HDC dc = GetDC(window);
        HGDIOBJ old = SelectObject(dc, Font());
        int maxWidth = std::max(Scaled(200, dpi), static_cast<int>(area.right - area.left) - Scaled(48, dpi));
        RECT measured{ 0, 0, maxWidth - Scaled(32, dpi), 0 };
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        ReleaseDC(window, dc);
        int width = measured.right + Scaled(32, dpi);
        int height = measured.bottom + Scaled(20, dpi);
        int x = (area.left + area.right - width) / 2;
        int y = area.bottom - height - Scaled(24, dpi);
        SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    }

    void Toast::Paint()
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT rc;
        GetClientRect(window, &rc);
        HBRUSH brush = CreateSolidBrush(Background(kind));
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        UINT dpi = GetDpiForWindow(window);
        InflateRect(&rc, -Scaled(16, dpi), -Scaled(10, dpi));
        HGDIOBJ old = SelectObject(dc, Font());
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc, DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(window, &ps);
    }

    LRESULT Toast::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_TIMER:
        case WM_LBUTTONUP:
            KillTimer(window, CloseTimer);
            ShowWindow(window, SW_HIDE);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE; // a click closes it without taking focus from the document
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK Toast::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto self = reinterpret_cast<Toast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->window)
        {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("toast: message 0x{:X}", message));
            return 0;
        }
    }
}
