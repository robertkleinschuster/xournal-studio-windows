#pragma once

// Carries out what the explorer asks for - open, new document/folder, rename, delete, discard changes, cut/copy/paste,
// show in File Explorer, copy as path - taking care of the open document when it is affected (the leave question before
// it is left; reopened from its new place after a rename; following its files when they are moved; left before its
// file is deleted or given its committed content back).
// Each command acts on exactly the item it was given. Nothing that changes files runs during a sync.

#include "DocumentSession.h"
#include "ExplorerTree.h"
#include "ProjectExplorer.h"
#include "SyncController.h"
#include "Toast.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace xournal::app
{
    class ExplorerCommands
    {
    public:
        using Messages = std::function<void(std::wstring const&, ToastKind)>;

        ExplorerCommands(HWND owner, DocumentSession& session, SyncController& sync, ProjectExplorer& explorer,
            ExplorerTree& tree, Messages messages);

        void Run(ExplorerRequest const& request);

        /// .xopp, .moj and .pdf open here (after the leave question), at `page` (0-based; -1: where it opens); everything
        /// else in the app Windows has for it.
        void Open(std::filesystem::path const& file, int page = -1);
        /// The session shows another document (or saved it): a page asked for by Open is scrolled to now.
        void DocumentShown();
        std::function<void(int page)> scrollToPage;

        /// Gives the keyboard to the document after a document was opened (Entf is then meant for its selection).
        std::function<void()> focusDocument;

    private:
        void NewDocument(std::filesystem::path const& folder);
        void NewFolder(std::filesystem::path const& folder);
        void Rename(std::filesystem::path const& item, bool isFolder, std::wstring const& edited);
        void Delete(std::filesystem::path const& item, bool isFolder);
        void Discard(std::filesystem::path const& item);
        void PutOnClipboard(std::filesystem::path const& item, bool move);
        void Paste(std::filesystem::path const& folder);
        void ShowInExplorer(std::filesystem::path const& item);
        void CopyPath(std::filesystem::path const& item);
        std::optional<std::wstring> NameError(std::filesystem::path const& folder, std::wstring const& name,
            std::filesystem::path const& current) const;

        HWND owner;
        DocumentSession& session;
        SyncController& sync;
        ProjectExplorer& explorer;
        ExplorerTree& tree;
        Messages messages;
        std::optional<std::pair<std::filesystem::path, int>> pendingPage; // opened from a task's link, still loading
    };
}
