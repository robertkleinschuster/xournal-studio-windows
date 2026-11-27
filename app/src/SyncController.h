#pragma once

// Git sync of the project folder (Datei → Synchronisieren, the explorer's button): asks to save changes first, then
// commits everything, pulls with rebase and pushes (services' GitSync, on the thread pool), and reloads the document if
// the pull changed it - or, with unsaved notes in it, lets the user decide (reload / keep mine / save mine as a copy).
// While it runs, nothing that writes project files starts: RefusedWhileSyncing says so.

#include "DocumentSession.h"
#include "MessageKind.h"

#include "xournal/git/GitSync.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace xournal::app
{
    class SyncController
    {
    public:
        using Messages = std::function<void(std::wstring const&, MessageKind)>;

        SyncController(HWND owner, DocumentSession& session, Messages messages);

        bool IsSyncing() const { return syncing; }

        /// Syncs the repository holding `folder` (nothing: no project folder - said).
        void Sync(std::optional<std::filesystem::path> const& folder);

        /// Only fetches and brings the server's commits in (Datei → Vom Server holen): no commit, no upload, no question
        /// about saving first; the same handling of the open document if the pull changed it.
        void Pull(std::optional<std::filesystem::path> const& folder);

        /// While a sync runs: tells the user that `what` waits for it ("Öffnen", "Das Ändern von Dateien") and returns
        /// true.
        bool RefusedWhileSyncing(std::wstring const& what) const;

        /// While a sync runs: the document is saved once it ends (shown in the status bar) - unless the sync changed it, then the
        /// user decides again. True when the save was put off.
        bool DeferSave();

        /// A sync started or ended (menu, button, explorer refresh).
        std::function<void()> stateChanged;
        /// A sync ended; true when it succeeded (it fetched, so the server's state is fresh).
        std::function<void(bool succeeded)> finished;

    private:
        void Run(std::optional<std::filesystem::path> const& folder, bool pullOnly);
        void Finish(git::SyncResult const& result, bool pullOnly);
        bool ResolvePulledDocument(std::shared_ptr<XoppDocument> const& document, int position);
        void SetSyncing(bool value);
        void SaveIfRequested();

        HWND owner;
        DocumentSession& session;
        Messages messages;
        bool syncing = false;
        std::shared_ptr<XoppDocument> saveFor; // the document Strg+S was pressed for during the sync
        bool saveRequested = false; // Strg+S during the sync
    };
}
