#pragma once

// The upstream branch's remote for this module: fetch and push with the app's credential provider (exceptions never
// cross libgit2's C frames: a callback's error is kept and thrown after the call). Internal.

#include "LibGit2.h"

#include "xournal/git/GitSync.h"

#include <functional>
#include <mutex>

namespace xournal::git::upstream
{
    /// Held for every network operation (fetch, sync): a background fetch and a sync never update the same references at
    /// the same time.
    std::mutex& NetworkLock();

    /// The remote the branch `head` tracks (branch.<name>.remote).
    git2::Remote RemoteOf(git_repository* repo, git_reference* head);

    /// Fetches the remote's configured refspecs: only remote-tracking references change, never the working tree.
    /// `cancelled` (may be empty) is asked by the progress callbacks; yes stops the fetch (Error, E_ABORT).
    void Fetch(git_remote* remote, CredentialProvider const& credentials, std::function<bool()> const& cancelled = {});

    /// Pushes `head` to its upstream branch. Returns what the remote refused ("<ref>: <reason>"), empty when it took
    /// everything - a rejection is usually another client's push since the last fetch, so the caller fetches and retries.
    /// Transport and sign-in errors are thrown.
    std::string Push(git_repository* repo, git_remote* remote, git_reference* head, CredentialProvider const& credentials);
}
