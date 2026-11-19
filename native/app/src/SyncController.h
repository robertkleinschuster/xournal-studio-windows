#pragma once

// Git sync of the project folder (Datei → Synchronisieren, the explorer's button): asks to save changes first, then
// commits everything, pulls with rebase and pushes (services' GitSync, on the thread pool), and reloads the document if
// the pull changed it - or, with unsaved notes in it, lets the user decide (reload / keep mine / save mine as a copy).
// While it runs, nothing that writes project files starts: RefusedWhileSyncing says so.

#include "DocumentSession.h"
#include "Toast.h"

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
        using Messages = std::function<void(std::wstring const&, ToastKind)>;

        SyncController(HWND owner, DocumentSession& session, Messages messages);

        bool IsSyncing() const { return syncing; }

        /// Syncs the repository holding `folder` (nothing: no project folder - said).
        void Sync(std::optional<std::filesystem::path> const& folder);

        /// While a sync runs: tells the user that `what` waits for it ("Öffnen", "Das Ändern von Dateien") and returns
        /// true.
        bool RefusedWhileSyncing(std::wstring const& what) const;

        /// While a sync runs: the document is saved once it ends (said in a toast) - unless the sync changed it, then the
        /// user decides again. True when the save was put off.
        bool DeferSave();

        /// A sync started or ended (menu, button, explorer refresh).
        std::function<void()> stateChanged;

    private:
        void Finish(git::SyncResult const& result);
        bool ResolvePulledDocument();
        void SetSyncing(bool value);
        void SaveIfRequested();

        HWND owner;
        DocumentSession& session;
        Messages messages;
        bool syncing = false;
        bool saveRequested = false; // Strg+S during the sync
    };
}
