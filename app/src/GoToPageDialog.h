#pragma once

// Gehe zu Seite (Strg+G): a page number from 1 to the page count in a number box with arrows (comctl32 up-down).

#include <windows.h>

#include <optional>

namespace xournal::app::GoToPageDialog
{
    /// The chosen page (0-based); nothing when cancelled. `current` is shown first (0-based).
    std::optional<int> Ask(HWND owner, int current, int count);
}
