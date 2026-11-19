#pragma once

// Keeps the recovery copy (Recovery) of the open document up to date. The app decides when (a few seconds after a
// change, at once when the window loses focus) and calls WriteNow; this class has no timer. Snapshots are taken on the
// caller's thread (the ink's thread); writes and deletes run in order on a worker thread of its own, so a delete never
// overtakes a write still running.
//
// Failures never end the app and are never lost: each is logged and handed to `failed` - on the caller's thread when
// the snapshot failed (the changes stay pending and are tried again with the next WriteNow), on the worker thread when a
// write or delete failed (the app marshals it to its UI).

#include "xournal/recovery/Recovery.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace xournal::recovery
{
    class Autosave
    {
    public:
        struct Copy
        {
            XmlElementPtr snapshot; // detached (TakeSnapshot)
            RecoveryInfo info;
        };

        /// The document as it is now; called on the thread that calls WriteNow/WriteNowReplacing.
        using TakeCopy = std::function<Copy()>;
        using Failed = std::function<void(std::string const& what, std::exception_ptr error)>;

        Autosave(Recovery recovery, TakeCopy takeCopy, Failed failed);
        /// Finishes everything queued (a last delete included), then stops the worker.
        ~Autosave();
        Autosave(Autosave const&) = delete;
        Autosave& operator=(Autosave const&) = delete;

        /// The current document's copy id.
        std::wstring const& Id() const { return id; }

        /// The document has changes the copy does not have yet (the app starts its delay).
        void Changed() { pending = true; }
        bool Pending() const { return pending; }

        /// Writes pending changes now (nothing pending: nothing happens).
        void WriteNow();

        /// Writes the document now and then deletes `oldId` (a restored copy) - only if the write succeeded, so there is
        /// always one complete copy.
        void WriteNowReplacing(std::wstring const& oldId);

        /// Nothing to recover any more (saved, or discarded by the user): the copy goes.
        void Discard();

        /// Another document is shown: the previous one's copy goes, the new one gets its own id.
        void StartNewDocument();

        /// Blocks until all queued writes and deletes are done.
        void WaitIdle();

    private:
        using Work = std::function<void()>;

        bool Take(Copy& copy, std::string const& what);
        void Enqueue(std::string what, Work work);
        void Run() noexcept;
        void Report(std::string const& what, std::exception_ptr error) noexcept;

        Recovery recovery;
        TakeCopy takeCopy;
        Failed failed;
        std::wstring id = Recovery::NewId();
        bool pending = false;

        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable idle;
        std::deque<std::pair<std::string, Work>> queue;
        bool busy = false;
        bool stopping = false;
        std::thread worker;
    };
}
