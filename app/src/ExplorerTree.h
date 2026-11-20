#pragma once

// The project folder as a TreeView (comctl32, File Explorer's theme and the shell's system icons): every file and folder
// of a ProjectSnapshot, files and folders with git changes colored as code editors' git views do (new green, modified
// blue, deleted red and struck through - deleted files of the last commit stay listed at their place so they can be
// restored; lighter on a dark window background). It keeps the open folders and the selection across refreshes. It
// only shows and asks: what the user wants (open, the context menu's commands, F2/Entf/Strg+Umschalt+C and - routed by
// the window - Strg+X/C/V, a renamed label) goes to `requested`; ExplorerCommands carries it out.

#include "ProjectSnapshot.h"

#include <windows.h>
#include <commctrl.h>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace xournal::app
{
    enum class ExplorerCommand
    {
        Open,
        NewDocument,
        NewFolder,
        Rename,        // with the new name, from the label edit
        Delete,
        Discard,       // a file, or a folder with everything in it
        ShowInExplorer,
        CopyPath,
        Cut,
        Copy,
        Paste,         // into the folder, or the file's folder
    };

    struct ExplorerRequest
    {
        ExplorerCommand command;
        std::filesystem::path path;   // the item (the project folder for the empty space)
        bool isFolder = false;
        std::wstring newName;         // Rename: the name as edited (a file without its extension)
    };

    class ExplorerTree
    {
    public:
        ExplorerTree(HWND parent, int id);
        ~ExplorerTree();
        ExplorerTree(ExplorerTree const&) = delete;
        ExplorerTree& operator=(ExplorerTree const&) = delete;

        HWND Window() const { return tree; }
        bool HasFocus() const;

        /// Shows the snapshot (open folders and selection kept by path).
        void Show(ProjectSnapshot snapshot);
        ProjectSnapshot const& Snapshot() const { return snapshot; }

        /// Selects this item at the next Show (or now, if shown), e.g. a folder just created or renamed.
        void Select(std::filesystem::path const& p);
        /// Starts editing the label of this item (rename), e.g. a document just created.
        void BeginRename(std::filesystem::path const& p);

        /// The selected item's request for `command` (nothing selected: nothing happens, but Paste goes into the project
        /// folder).
        void RequestForSelection(ExplorerCommand command);

        std::function<void(ExplorerRequest const&)> requested;

        /// The parent forwards the tree's WM_NOTIFY and WM_CONTEXTMENU here.
        LRESULT OnNotify(NMHDR const& header);
        void OnContextMenu(LPARAM lp);

    private:
        struct Node
        {
            std::filesystem::path path;
            bool isFolder = false;
            bool missing = false; // deleted, shown to be restored
        };

        void Add(HTREEITEM parent, project::ProjectEntry const& entry);
        Node const* NodeOf(HTREEITEM item) const;
        HTREEITEM Find(std::filesystem::path const& p) const;
        void Request(ExplorerCommand command, Node const& node, std::wstring newName = {});
        void ShowMenu(Node const& node, bool onItem, POINT screen);
        LRESULT CustomDraw(NMTVCUSTOMDRAW& draw);
        void UpdateStruckFont();
        int Icon(std::wstring const& name, bool folder, bool open);
        static bool HasOwnIcon(project::ProjectEntry const& entry);

        HWND tree = nullptr;
        ProjectSnapshot snapshot;
        std::vector<Node> nodes;                                       // lParam of each item = its index
        std::set<std::wstring, LessIgnoreCase> expanded;               // open folders, by path
        std::optional<std::filesystem::path> pendingSelection;
        std::optional<ProjectSnapshot> deferred;                       // arrived while a name was being edited
        std::map<std::wstring, int, LessIgnoreCase> icons;             // system image list index per type
        HFONT struckFont = nullptr;                                    // the tree's font struck through (deleted items)
        LOGFONTW struckFrom{};                                         // what it was made from
    };
}
