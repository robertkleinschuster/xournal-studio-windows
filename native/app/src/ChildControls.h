#pragma once

// Creating the panes' common controls (comctl32) as children, and giving them the window's message font (comctl32 v6
// does not pick it up by itself).

#include <windows.h>

namespace xournal::app::ChildControls
{
    /// A visible child control; throws when it cannot be created.
    HWND Create(HWND parent, wchar_t const* type, wchar_t const* text, DWORD style, int id, DWORD exStyle = 0);

    /// The message font of the parent's DPI (one per process, kept until exit); a failure is logged.
    void UseMessageFont(HWND control);
}
