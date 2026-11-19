#pragma once

// A folder as it is on disk, for the explorer: every file and folder, except what Windows itself marks hidden or system;
// links (reparse points) are shown but not entered. Sorted folders first, then files, by name as File Explorer sorts
// (StrCmpLogicalW: numbers by value, "Vorlesung 2" before "Vorlesung 11").

#include <filesystem>
#include <string>
#include <vector>

namespace xournal::app
{
    struct FolderEntry
    {
        std::wstring name;
        std::filesystem::path path;
        bool folder = false; // a folder that can be entered
    };

    namespace FolderListing
    {
        std::vector<FolderEntry> List(std::filesystem::path const& folder);
    }
}
