#pragma once

// A project folder as it is on disk, for the explorer: every folder and file, nothing paired or filtered by the app.
// Only what Windows itself marks hidden or system (as File Explorer's default view, e.g. .git) is left out. Links
// (junctions, symbolic links) are shown but not entered: one pointing to a parent would recurse forever.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::project
{
    struct ProjectEntry
    {
        std::filesystem::path path;
        bool isFolder = false;
        std::vector<ProjectEntry> children; // folders first, then files, each in NameOrder

        std::wstring Name() const { return path.filename().native(); }
    };

    namespace ProjectTree
    {
        /// The order File Explorer shows names in: the user's language, case ignored, digits compared as numbers
        /// (CompareStringEx with SORT_DIGITSASNUMBERS: "Vorlesung 2" before "Vorlesung 11"); names equal so (only case
        /// differs) follow in ordinal order, so the tree keeps its order between refreshes. <0, 0, >0.
        int CompareNames(std::wstring_view a, std::wstring_view b);

        /// The folder and everything below it. A folder that cannot be read (access denied, gone) is logged and shown
        /// empty.
        ProjectEntry Read(std::filesystem::path const& folder);
    }
}
