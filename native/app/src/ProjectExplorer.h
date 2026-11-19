#pragma once

// Keeps the explorer tree up to date with the project folder: reads it (files and git status) on the thread pool and
// shows the result; refreshes 400 ms after the last change on disk (FolderWatcher), and when the app asks (a save, the
// window activated, a sync). A refresh that finishes after a later one began is dropped.

#include "DelayedAction.h"
#include "ExplorerTree.h"

#include "xournal/project/FolderWatcher.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace xournal::app
{
    class ProjectExplorer
    {
    public:
        explicit ProjectExplorer(ExplorerTree& tree);
        ~ProjectExplorer();
        ProjectExplorer(ProjectExplorer const&) = delete;
        ProjectExplorer& operator=(ProjectExplorer const&) = delete;

        std::optional<std::filesystem::path> const& Folder() const { return folder; }
        void SetFolder(std::optional<std::filesystem::path> folder);

        /// Reads the folder again soon (debounced).
        void Refresh();
        /// Reads the folder now, on the UI thread (after a command, so the tree shows its result at once).
        void RefreshNow();

        /// After the tree showed a new reading of the folder (e.g. the tasks pane reads its todo.txt again).
        std::function<void()> refreshed;

    private:
        void ReadInBackground();

        ExplorerTree& tree;
        std::optional<std::filesystem::path> folder;
        std::unique_ptr<project::FolderWatcher> watcher;
        DelayedAction delayed;
        std::shared_ptr<std::uint64_t> generation = std::make_shared<std::uint64_t>(0); // UI thread
    };
}
