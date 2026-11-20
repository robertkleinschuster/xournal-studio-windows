#pragma once

// The project folder as the explorer shows it at one moment: everything on disk (services' ProjectTree) and the git
// state of its files and folders (GitSync status with folders) with the counts the sync button shows. Read on a worker
// thread, shown on the UI thread.

#include "xournal/git/GitSync.h"
#include "xournal/git/SyncStatus.h"
#include "xournal/project/ProjectTree.h"

#include <filesystem>
#include <optional>

namespace xournal::app
{
    struct ProjectSnapshot
    {
        std::optional<std::filesystem::path> folder;    // nothing: no project folder chosen
        project::ProjectEntry root;
        std::optional<std::filesystem::path> gitRoot;   // the repository's working directory, if the folder is in one
        git::StatusMap status;                          // files and folders with changes (full paths)
        git::SyncStatus sync;                           // for the sync button (remoteKnown is the app's to set)

        git::FileState StateOf(std::filesystem::path const& p) const;

        /// Reads the folder and its git status (any thread). A git error is logged and leaves the status empty (the
        /// files are still shown).
        static ProjectSnapshot Read(std::optional<std::filesystem::path> const& folder);
    };
}
