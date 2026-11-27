#pragma once

// Crash recovery of the app (services' Recovery and Autosave): while an open document (any tab) has unsaved changes, a
// copy is written 5 s after the first change and whenever the window loses focus; it goes once the changes are saved or
// discarded (tab closed, document replaced, "Nicht speichern", window closed). At the start, copies of sessions that ended
// without that decision are offered (Wiederherstellen / Verwerfen = recycle bin / Später). Windows restarts the app
// after a crash (RegisterApplicationRestart); the start then offers the copy.

#include "DelayedAction.h"
#include "DocumentSession.h"
#include "DocumentView.h"
#include "MessageKind.h"

#include "xournal/recovery/Autosave.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace xournal::app
{
    class RecoveryCopies
    {
    public:
        using Messages = std::function<void(std::wstring const&, MessageKind)>;

        RecoveryCopies(HWND owner, std::filesystem::path const& appFolder, DocumentSession& session, DocumentView& view,
            Messages messages);
        ~RecoveryCopies();
        RecoveryCopies(RecoveryCopies const&) = delete;
        RecoveryCopies& operator=(RecoveryCopies const&) = delete;

        /// Offers the copies of ended sessions, newest first; true when one was restored (each is open in a tab, unsaved).
        bool OfferAtStart();

        /// The document or its saved state changed (call on every change of the title).
        void Update();
        /// The window lost focus (maybe for good: shutdown, log off): the copy is brought up to date now.
        void Deactivated();
        /// The window closes after the user decided about the changes: the copy goes (waits for the queue).
        void End();

    private:
        struct Entry
        {
            std::shared_ptr<XoppDocument> doc;
            std::unique_ptr<recovery::Autosave> autosave;
            bool copyWanted = false; // a copy was asked for (written or pending) since the last discard
        };

        bool Offer(recovery::RecoveryEntry const& entry);
        recovery::Autosave::Copy TakeCopy(std::shared_ptr<XoppDocument> const& doc);
        Entry& EntryOf(std::shared_ptr<XoppDocument> const& doc);
        void WriteAll();

        HWND owner;
        DocumentSession& session;
        DocumentView& view;
        Messages messages;
        recovery::Recovery store;
        recovery::ProcessStamp process;
        std::vector<std::unique_ptr<Entry>> entries; // one per open document (tab)
        DelayedAction delayed;
    };
}
