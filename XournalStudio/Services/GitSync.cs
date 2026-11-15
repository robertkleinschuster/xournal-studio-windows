using LibGit2Sharp;
using LibGit2Sharp.Handlers;

namespace XournalStudio.Services;

public enum GitFileState
{
    Unchanged,
    New,
    Modified,
}

/// <summary>Outcome of a sync: documents the pull changed (full paths) and whether commits were pushed.</summary>
public sealed record GitSyncResult(IReadOnlyList<string> PulledFiles, bool Committed, bool Pushed);

public sealed class GitSyncConflictException(string message) : Exception(message);

/// <summary>
/// The git repository of the project folder (LibGit2Sharp): file status for the explorer and the one-button sync of
/// the GTK Xournal Studio – commit everything, pull with rebase, push.
/// </summary>
public static class GitSync
{
    /// <summary>The working directory of the repository containing <paramref name="path"/>, or null.</summary>
    public static string? FindRoot(string path)
    {
        string? gitDir = Repository.Discover(path);
        if (gitDir == null)
        {
            return null;
        }
        using var repo = new Repository(gitDir);
        return repo.Info.WorkingDirectory is { } dir ? Path.TrimEndingDirectorySeparator(dir) : null;
    }

    /// <summary>New and modified files of the repository by full path (case-insensitive, like the file system).</summary>
    public static Dictionary<string, GitFileState> GetStatus(string root)
    {
        var states = new Dictionary<string, GitFileState>(StringComparer.OrdinalIgnoreCase);
        using var repo = new Repository(root);
        var options = new StatusOptions { IncludeUntracked = true, RecurseUntrackedDirs = true };
        foreach (var entry in repo.RetrieveStatus(options))
        {
            var state =
                entry.State.HasFlag(FileStatus.NewInWorkdir) || entry.State.HasFlag(FileStatus.NewInIndex) ? GitFileState.New :
                entry.State.HasFlag(FileStatus.ModifiedInWorkdir) || entry.State.HasFlag(FileStatus.ModifiedInIndex) ||
                entry.State.HasFlag(FileStatus.RenamedInWorkdir) || entry.State.HasFlag(FileStatus.RenamedInIndex) ||
                entry.State.HasFlag(FileStatus.TypeChangeInWorkdir) || entry.State.HasFlag(FileStatus.TypeChangeInIndex)
                    ? GitFileState.Modified : GitFileState.Unchanged;
            if (state != GitFileState.Unchanged)
            {
                states[FullPath(repo, entry.FilePath)] = state;
            }
        }
        return states;
    }

    /// <summary>
    /// Commits all changes of the working directory ("Sync from &lt;machine&gt; (date)"), fetches, rebases local
    /// commits onto the upstream branch (fast-forward when there are none) and pushes. A rebase conflict is aborted
    /// (the local commit stays) and reported as <see cref="GitSyncConflictException"/>.
    /// </summary>
    public static GitSyncResult Sync(string root, CredentialsHandler? credentials, string machineName, DateTimeOffset now)
    {
        using var repo = new Repository(root);
        var branch = repo.Head;
        if (branch.IsTracking == false || branch.TrackedBranch == null)
        {
            throw new InvalidOperationException($"Der Zweig „{branch.FriendlyName}“ hat keinen Upstream-Zweig.");
        }
        var signature = repo.Config.BuildSignature(now) ??
            throw new InvalidOperationException("Git-Name und -E-Mail sind nicht eingestellt (user.name, user.email).");

        bool committed = false;
        if (repo.RetrieveStatus(new StatusOptions { IncludeUntracked = true, RecurseUntrackedDirs = true }).IsDirty)
        {
            Commands.Stage(repo, "*");
            repo.Commit($"Sync from {machineName} ({now:yyyy-MM-dd HH:mm})", signature, signature);
            committed = true;
        }

        var remote = repo.Network.Remotes[branch.RemoteName];
        var refSpecs = remote.FetchRefSpecs.Select(r => r.Specification);
        Commands.Fetch(repo, remote.Name, refSpecs, new FetchOptions { CredentialsProvider = credentials }, null);

        branch = repo.Head;
        var upstream = branch.TrackedBranch;
        var before = branch.Tip;
        var tracking = branch.TrackingDetails;
        if (tracking.BehindBy > 0 && tracking.AheadBy > 0)
        {
            RebaseResult result;
            try
            {
                result = repo.Rebase.Start(branch, upstream, null, new Identity(signature.Name, signature.Email),
                    new RebaseOptions());
            }
            catch (Exception)
            {
                // E.g. a file locked by another program: never leave the repository mid-rebase (detached HEAD).
                if (repo.Info.CurrentOperation is CurrentOperation.RebaseMerge or CurrentOperation.Rebase
                    or CurrentOperation.RebaseInteractive)
                {
                    repo.Rebase.Abort();
                }
                throw;
            }
            if (result.Status != RebaseStatus.Complete)
            {
                var conflicts = repo.Index.Conflicts.Select(c => (c.Ours ?? c.Theirs ?? c.Ancestor).Path).ToList();
                repo.Rebase.Abort();
                throw new GitSyncConflictException(conflicts.Count > 0
                    ? "Konflikt in " + string.Join(", ", conflicts)
                    : "Die eigenen Änderungen ließen sich nicht auf den neuen Stand setzen.");
            }
        }
        else if (tracking.BehindBy > 0)
        {
            repo.Merge(upstream.Tip, signature, new MergeOptions { FastForwardStrategy = FastForwardStrategy.FastForwardOnly });
        }

        var pulled = new List<string>();
        branch = repo.Head;
        if (branch.Tip.Sha != before.Sha)
        {
            // The files the upstream commits changed (the merge base of before and upstream is where they start).
            var mergeBase = repo.ObjectDatabase.FindMergeBase(before, upstream.Tip);
            var changes = repo.Diff.Compare<TreeChanges>(mergeBase?.Tree, upstream.Tip.Tree);
            pulled.AddRange(changes.Select(c => FullPath(repo, c.Path)));
        }

        bool pushed = false;
        if (branch.TrackingDetails.AheadBy > 0)
        {
            repo.Network.Push(branch, new PushOptions { CredentialsProvider = credentials });
            pushed = true;
        }
        return new GitSyncResult(pulled, committed, pushed);
    }

    private static string FullPath(Repository repo, string relative) =>
        Path.GetFullPath(Path.Combine(repo.Info.WorkingDirectory, relative));
}
