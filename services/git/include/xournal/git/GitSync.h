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
    };

    using StatusMap = std::map<std::wstring, FileState, LessIgnoreCase>;

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

        /// New and modified (also deleted, renamed) files of the repository by full path. Deleted files are not shown in
        /// the explorer, but they mark their folders as modified (WithFolders).
        StatusMap GetStatus(path const& root);

        /// Adds the folders below `root` that contain changes: modified when anything in them is modified, renamed or
        /// deleted, otherwise new (only new files in them), as the git views of code editors show it.
        StatusMap WithFolders(StatusMap const& status, path const& root);

        /// Commits all changes of the working directory ("Sync from <machine> (yyyy-MM-dd HH:mm)"), fetches, rebases
        /// local commits onto the upstream branch (fast-forward when there are none) and pushes. `credentials` may be
        /// empty (remotes that need no sign-in).
        SyncResult Sync(path const& root, CredentialProvider const& credentials, std::wstring const& machineName, Moment now);

        /// Discards the local changes of one file: it gets its content of the last commit (HEAD) back, in the index too
        /// (git checkout HEAD -- file). The committed path is found ignoring case like Windows, but never guessed among
        /// several names that differ only in case. Throws when the file is not in the last commit (a new file has
        /// nothing to go back to).
        void Discard(path const& root, path const& file);
    }
}
