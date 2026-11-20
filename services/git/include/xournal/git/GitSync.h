#pragma once

// The git repository of the project folder (libgit2): file status for the explorer and the one-button sync of the GTK
// Xournal Studio - commit everything, pull with rebase, push. Paths are full Windows paths; maps keyed by them ignore
// case like the file system. Failures throw xournal::Error (libgit2's message, logged where thrown); a rebase conflict
// throws SyncConflict after the rebase was aborted - the repository is never left mid-rebase.
//
// Threads: every call opens the repository itself; run them off the UI thread (sync talks to the network).

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xournal::git
{
    using std::filesystem::path;

    /// Ordered for folders: Modified > New (WithFolders).
    enum class FileState
    {
        Unchanged,
        New,
        Modified,
        /// Shown as modified, but its path is new (a rename): there is no committed content to go back to.
        Renamed,
        /// In the last commit, but deleted (on disk or in the index): it can be restored. Folders show it as modified.
        Deleted,
    };

    using StatusMap = std::map<std::wstring, FileState, LessIgnoreCase>;

    /// What "Änderungen verwerfen" does for a file or folder (full paths, git's letter case): files of the last commit get
    /// its content back (modified ones) or come back (deleted ones); new files - not in the last commit - have nothing
    /// to go back to and go to the recycle bin (the app's part; Discard takes them out of the index only).
    struct DiscardPlan
    {
        std::vector<path> restore;
        std::vector<path> remove;

        bool Empty() const { return restore.empty() && remove.empty(); }
    };

    /// A user name and password (or token) for an HTTPS remote.
    struct Credential
    {
        std::string userName; // UTF-8, as libgit2 takes it
        std::string password;
    };

    /// Asked when a remote wants to sign in: the remote's URL and the user name in it (may be empty). Throwing ends the
    /// fetch or push with that error.
    using CredentialProvider = std::function<Credential(std::string const& url, std::string const& userNameFromUrl)>;

    /// When the sync happens: the commit's time and the time zone offset its message and signature use.
    struct Moment
    {
        std::chrono::sys_seconds utc;
        std::chrono::minutes offset{ 0 };

        static Moment Now(); // the system clock and the current time zone
    };

    /// Outcome of a sync: documents the pull changed (full paths) and whether commits were made and pushed.
    struct SyncResult
    {
        std::vector<path> pulledFiles;
        bool committed = false;
        bool pushed = false;
    };

    /// How the branch and its upstream branch differ, as of the last fetch: commits to push and commits to pull.
    struct Divergence
    {
        size_t ahead = 0;
        size_t behind = 0;

        bool operator==(Divergence const&) const = default;
    };

    /// The rebase onto the remote's commits conflicted and was aborted (the local commit stays). E_ABORT.
    class SyncConflict : public Error
    {
    public:
        explicit SyncConflict(std::string const& message);
    };

    namespace GitSync
    {
        /// The working directory of the repository containing `p` (without a trailing separator), or nothing.
        std::optional<path> FindRoot(path const& p);

        /// New, modified, renamed and deleted files of the repository by full path. The explorer shows deleted files at
        /// their place (to restore them); they mark their folders as modified (WithFolders).
        StatusMap GetStatus(path const& root);

        /// Adds the folders below `root` that contain changes: modified when anything in them is modified, renamed or
        /// deleted, otherwise new (only new files in them), as the git views of code editors show it.
        StatusMap WithFolders(StatusMap const& status, path const& root);

        /// Commits all changes of the working directory ("Sync from <machine> (yyyy-MM-dd HH:mm)"), fetches, rebases
        /// local commits onto the upstream branch (fast-forward when there are none) and pushes. `credentials` may be
        /// empty (remotes that need no sign-in).
        SyncResult Sync(path const& root, CredentialProvider const& credentials, std::wstring const& machineName, Moment now);

        /// Commits HEAD's branch has that its upstream branch has not (ahead) and the other way round (behind), from the
        /// local references (no network: "behind" is as of the last fetch). Nothing when HEAD is unborn, detached or has
        /// no upstream branch.
        std::optional<Divergence> GetDivergence(path const& root);

        /// Fetches the remote of HEAD's upstream branch: only the remote-tracking references change, never the working
        /// tree, the index or a local branch. False (nothing fetched) when there is no upstream branch. A fetch and a sync
        /// of this process never run at the same time (the later one waits).
        bool Fetch(path const& root, CredentialProvider const& credentials);

        /// What discarding the changes of `item` - a file, or a folder with everything in it - would do (ignoring case
        /// like Windows). Empty when it has no changes.
        DiscardPlan PlanDiscard(path const& root, path const& item);

        /// Carries out the git part of a plan: the `restore` files get their content of the last commit (HEAD) back, in
        /// the index too (git checkout HEAD -- files; deleted ones come back); the `remove` files are taken out of the
        /// index if they were added there (their files stay - recycling them is the caller's).
        void Discard(path const& root, DiscardPlan const& plan);
    }
}
