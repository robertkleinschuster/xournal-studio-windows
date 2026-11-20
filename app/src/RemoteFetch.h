#pragma once

// Keeps the remote-tracking references of the project folder's repository fresh, so the sync button can show the
// commits to pull: fetches in the background (GitSync::Fetch with the Credential Manager's sign-in - never asking) when
// the folder is set and when the window is activated, at most every five minutes (a time check, no timer); a sync counts
// as a fetch, and a sync that starts cancels a running fetch (it would wait for it otherwise). A failed fetch is logged
// and marks the server's state unknown - no message. The result is posted back to the UI thread. Fetching only updates
// remote-tracking references, never the working tree.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace xournal::app
{
    class RemoteFetch
    {
    public:
        RemoteFetch() = default;
        ~RemoteFetch();
        RemoteFetch(RemoteFetch const&) = delete;
        RemoteFetch& operator=(RemoteFetch const&) = delete;

        /// Another project folder (or none): fetches it at once.
        void SetFolder(std::optional<std::filesystem::path> folder);
        /// Fetches unless a fetch runs or the last one began less than five minutes ago.
        void FetchIfDue();
        /// A sync starts: a running fetch stops (libgit2's progress callbacks), so the sync does not wait behind it.
        void Cancel();
        /// A sync ended: when it succeeded it fetched, so the server's state is known and fresh.
        void SyncEnded(bool succeeded);

        /// False after a failed fetch (until one succeeds).
        bool RemoteKnown() const { return remoteKnown; }

        /// A fetch ended (the explorer reads the counts again).
        std::function<void()> fetched;

    private:
        void Start();

        std::optional<std::filesystem::path> folder;
        std::optional<std::chrono::steady_clock::time_point> lastFetch;
        bool fetching = false;
        bool remoteKnown = true;
        std::shared_ptr<std::atomic<bool>> cancelled; // of the running fetch
        std::shared_ptr<std::uint64_t> generation = std::make_shared<std::uint64_t>(0); // UI thread: a newer folder wins
    };
}
