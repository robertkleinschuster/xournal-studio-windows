#pragma once

// Files dropped on the window (WM_DROPFILES; the window called DragAcceptFiles): the documents among them that the app
// opens - .xopp, .moj and PDF - in the order dropped.

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <vector>

namespace xournal::app::FileDrop
{
    /// The dropped files that are documents; `others` counts the rest. Ends the drop (DragFinish).
    std::vector<std::filesystem::path> Documents(HDROP drop, size_t& others);
}
