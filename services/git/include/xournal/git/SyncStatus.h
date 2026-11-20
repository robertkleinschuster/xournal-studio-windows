#pragma once

// What the sync button shows about the project folder's repository, the way code editors show it next to their sync
// button: the number of changed files a sync would commit, the commits to push (↑) and the commits to pull (↓, as of
// the last fetch). Only the texts; reading the repository is GitSync's (GetStatus, GetDivergence, Fetch).

#include "xournal/git/GitSync.h"

#include <optional>
#include <string>

namespace xournal::git
{
    struct SyncStatus
    {
        bool repository = false;               // false: no project folder, not a git repository or not readable
        size_t changedFiles = 0;               // files a sync would commit (new, modified, deleted, renamed)
        std::optional<Divergence> divergence;  // nothing: no upstream branch
        bool remoteKnown = true;               // false: the last fetch failed - what the server has is unknown

        /// "Synchronisieren", followed by what is pending: "Synchronisieren   3  ↑1  ↓2" (changed files, commits to
        /// push, commits to pull; "↓?" when the server's state is unknown). Nothing pending: "Synchronisieren".
        std::wstring ButtonText() const;

        /// The same in words for the tooltip, one fact per line ("3 geänderte Dateien", "1 Commit zu senden",
        /// "2 Commits zu holen", "Stand des Servers unbekannt (Abruf fehlgeschlagen)"), or why there is nothing to show.
        std::wstring Details() const;
    };
}
