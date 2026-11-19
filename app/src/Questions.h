#pragma once

// Questions to the user as task dialogs (TaskDialogIndirect) with the app's own button texts: a choice among buttons and
// a confirmation of a destructive step. German texts come from the caller.

#include <windows.h>

#include <string>
#include <vector>

namespace xournal::app::Questions
{
    /// The buttons in order; returns the index of the one pressed, or -1 when the dialog was closed (Esc, X).
    /// `defaultButton`: the index Enter presses.
    int Choose(HWND owner, std::wstring const& title, std::wstring const& text, std::vector<std::wstring> const& buttons,
        int defaultButton = 0);

    /// "<verb>" / "Abbrechen", Abbrechen the default: true when the user confirmed.
    bool Confirm(HWND owner, std::wstring const& title, std::wstring const& text, std::wstring const& verb);
}
