#pragma once

// Creating the panes' common controls (comctl32) as children, and giving them the window's message font (comctl32 v6
// does not pick it up by itself).

#include <windows.h>

namespace xournal::app::ChildControls
{
    // Set or clear the C++ owner of a native window. Reports failures without throwing through a window procedure.
    bool SetWindowData(HWND window, void* owner) noexcept;

    /// A visible child control; throws when it cannot be created.
    HWND Create(HWND parent, wchar_t const* type, wchar_t const* text, DWORD style, int id, DWORD exStyle = 0);

    /// The message font for the control's DPI, kept until exit; a failure is logged.
    void UseMessageFont(HWND control);

    // Own a pane and its child HWNDs until the C++ pane constructor has finished.
    class PendingPaneWindow
    {
    public:
        explicit PendingPaneWindow(HWND window) : window(window) {}
        ~PendingPaneWindow();
        PendingPaneWindow(PendingPaneWindow const&) = delete;
        PendingPaneWindow& operator=(PendingPaneWindow const&) = delete;

        void Release() { window = nullptr; }

    private:
        HWND window;
    };

    // Batch native control updates without showing a hidden control or leaving redraw disabled on error.
    class BatchRedraw
    {
    public:
        explicit BatchRedraw(HWND control);
        ~BatchRedraw();
        BatchRedraw(BatchRedraw const&) = delete;
        BatchRedraw& operator=(BatchRedraw const&) = delete;

    private:
        HWND control = nullptr;
    };
}
