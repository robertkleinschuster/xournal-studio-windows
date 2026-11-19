#pragma once

// The project folder as a TreeView (comctl32): folders open on demand. What a folder holds comes from `list` (seam for
// the project service in native/services; until it lands: FolderListing). Double-click or Enter on a file asks the
// window to open it.

#include <windows.h>
#include <commctrl.h>

#include "FolderListing.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>

namespace xournal::app
{
    class ExplorerTree
    {
    public:
        ExplorerTree(HWND parent, int id);
        ExplorerTree(ExplorerTree const&) = delete;
        ExplorerTree& operator=(ExplorerTree const&) = delete;

        HWND Window() const { return tree; }

        /// Shows this folder (nothing: an empty tree with a hint).
        void SetFolder(std::optional<std::filesystem::path> folder);

        /// What a folder holds, in the order shown.
        std::function<std::vector<FolderEntry>(std::filesystem::path const&)> list = FolderListing::List;

        /// A file was chosen (double-click, Enter).
        std::function<void(std::filesystem::path const&)> fileChosen;

        /// The parent forwards the tree's WM_NOTIFY here; returns the result to give back.
        LRESULT OnNotify(NMHDR const& header);

    private:
        void AddChildren(HTREEITEM parent, std::filesystem::path const& folder);
        std::optional<std::filesystem::path> PathOf(HTREEITEM item) const;
        void ChooseSelected();

        HWND tree = nullptr;
        std::map<HTREEITEM, std::filesystem::path> paths;
        std::map<HTREEITEM, bool> folders;
    };
}
