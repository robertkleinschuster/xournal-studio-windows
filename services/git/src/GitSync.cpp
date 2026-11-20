#include "xournal/git/GitSync.h"

#include "LibGit2.h"
#include "Upstream.h"

#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"

#include <windows.h>

#include <format>

namespace xournal::git
{
    using namespace git2;

    namespace
    {
        [[noreturn]] void ThrowMessage(std::string const& message)
        {
            LogError(message);
            throw Error(message, E_FAIL);
        }

        Reference Head(git_repository* repo)
        {
            git_reference* head = nullptr;
            Check("HEAD", git_repository_head(&head, repo));
            return Reference(head);
        }

        StatusList Status(git_repository* repo, unsigned int extraFlags)
        {
            git_status_options options = GIT_STATUS_OPTIONS_INIT;
            options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
            options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS | extraFlags;
            git_status_list* list = nullptr;
            Check("status", git_status_list_new(&list, repo, &options));
            return StatusList(list);
        }

        FileState StateOf(unsigned int s)
        {
            if (s & (GIT_STATUS_WT_NEW | GIT_STATUS_INDEX_NEW))
            {
                return FileState::New;
            }
            if (s & (GIT_STATUS_WT_RENAMED | GIT_STATUS_INDEX_RENAMED))
            {
                return FileState::Renamed;
            }
            if (s & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_TYPECHANGE |
                GIT_STATUS_INDEX_TYPECHANGE))
            {
                return FileState::Modified;
            }
            if (s & (GIT_STATUS_WT_DELETED | GIT_STATUS_INDEX_DELETED))
            {
                return FileState::Deleted;
            }
            return FileState::Unchanged;
        }

        Signature MakeSignature(git_repository* repo, Moment now)
        {
            git_config* config = nullptr;
            Check("config", git_repository_config_snapshot(&config, repo));
            Config owned(config);
            char const* name = nullptr;
            char const* email = nullptr;
            if (git_config_get_string(&name, config, "user.name") < 0 || git_config_get_string(&email, config, "user.email") < 0)
            {
                ThrowMessage("Git-Name und -E-Mail sind nicht eingestellt (user.name, user.email).");
            }
            git_signature* signature = nullptr;
            Check("signature", git_signature_new(&signature, name, email, now.utc.time_since_epoch().count(),
                static_cast<int>(now.offset.count())));
            return Signature(signature);
        }

        Commit LookupCommit(git_repository* repo, git_oid const* id)
        {
            git_commit* commit = nullptr;
            Check("look up commit", git_commit_lookup(&commit, repo, id));
            return Commit(commit);
        }

        Tree TreeOf(git_commit* commit)
        {
            git_tree* tree = nullptr;
            Check("tree of commit", git_commit_tree(&tree, commit));
            return Tree(tree);
        }

        bool IsDirty(git_repository* repo)
        {
            StatusList list = Status(repo, 0);
            return git_status_list_entrycount(list.get()) > 0;
        }

        void CommitAll(git_repository* repo, git_signature const* signature, std::string const& message)
        {
            git_index* rawIndex = nullptr;
            Check("index", git_repository_index(&rawIndex, repo));
            Index index(rawIndex);
            char star[] = "*";
            char* patterns[] = { star };
            git_strarray all{ patterns, 1 };
            Check("stage all", git_index_add_all(index.get(), &all, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr));
            Check("stage deletions", git_index_update_all(index.get(), &all, nullptr, nullptr));
            Check("write index", git_index_write(index.get()));
            git_oid treeId{};
            Check("write tree", git_index_write_tree(&treeId, index.get()));
            git_tree* rawTree = nullptr;
            Check("look up tree", git_tree_lookup(&rawTree, repo, &treeId));
            Tree tree(rawTree);

            Commit parent;
            git_oid headId{};
            int found = git_reference_name_to_id(&headId, repo, "HEAD");
            if (found != GIT_ENOTFOUND)
            {
                Check("HEAD commit", found);
                parent = LookupCommit(repo, &headId);
            }
            git_commit const* parents[] = { parent.get() };
            git_oid commitId{};
            Check("commit", git_commit_create(&commitId, repo, "HEAD", signature, signature, nullptr, message.c_str(),
                tree.get(), parent ? 1 : 0, parent ? parents : nullptr));
        }

        struct Tracking
        {
            Reference head;
            Reference upstream;
            git_oid local{};
            git_oid remote{};
            size_t ahead = 0;
            size_t behind = 0;
        };

        Tracking Track(git_repository* repo)
        {
            Tracking t;
            t.head = Head(repo);
            git_reference* upstream = nullptr;
            int result = git_branch_upstream(&upstream, t.head.get());
            if (result == GIT_ENOTFOUND)
            {
                ThrowMessage(std::format("Der Zweig „{}“ hat keinen Upstream-Zweig.", git_reference_shorthand(t.head.get())));
            }
            Check("upstream branch", result);
            t.upstream.reset(upstream);
            git_oid const* local = git_reference_target(t.head.get());
            git_oid const* remote = git_reference_target(t.upstream.get());
            if (!local || !remote)
            {
                ThrowMessage("git: HEAD or its upstream branch is not a direct reference");
            }
            t.local = *local;
            t.remote = *remote;
            Check("ahead/behind", git_graph_ahead_behind(&t.ahead, &t.behind, repo, &t.local, &t.remote));
            return t;
        }

        bool IsMidRebase(git_repository* repo)
        {
            int state = git_repository_state(repo);
            return state == GIT_REPOSITORY_STATE_REBASE || state == GIT_REPOSITORY_STATE_REBASE_MERGE ||
                state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
        }

        // Never leave the repository mid-rebase (detached HEAD): called when anything went wrong during a rebase.
        void AbortRebase(git_repository* repo, git_rebase* rebase) noexcept
        {
            try
            {
                if (rebase)
                {
                    Check("abort rebase", git_rebase_abort(rebase));
                }
                else if (IsMidRebase(repo))
                {
                    git_rebase* opened = nullptr;
                    Check("open rebase", git_rebase_open(&opened, repo, nullptr));
                    Rebase owned(opened);
                    Check("abort rebase", git_rebase_abort(opened));
                }
            }
            catch (...)
            {
                LogException("git: the rebase could not be aborted - the repository needs \"git rebase --abort\"");
            }
        }

        std::vector<std::string> ConflictPaths(git_index* index)
        {
            std::vector<std::string> paths;
            git_index_conflict_iterator* raw = nullptr;
            Check("conflicts", git_index_conflict_iterator_new(&raw, index));
            ConflictIterator iterator(raw);
            git_index_entry const* ancestor = nullptr;
            git_index_entry const* ours = nullptr;
            git_index_entry const* theirs = nullptr;
            int result = 0;
            while ((result = git_index_conflict_next(&ancestor, &ours, &theirs, iterator.get())) == 0)
            {
                git_index_entry const* entry = ours ? ours : theirs ? theirs : ancestor;
                paths.emplace_back(entry->path);
            }
            if (result != GIT_ITEROVER)
            {
                Check("next conflict", result);
            }
            return paths;
        }

        [[noreturn]] void ThrowConflict(std::vector<std::string> const& paths)
        {
            std::string message;
            for (std::string const& p : paths)
            {
                message += (message.empty() ? "Konflikt in " : ", ") + p;
            }
            if (message.empty())
            {
                message = "Die eigenen Änderungen ließen sich nicht auf den neuen Stand setzen.";
            }
            LogError(message);
            throw SyncConflict(message);
        }

        // Rebases the local commits onto the upstream branch; a conflict is aborted and thrown as SyncConflict.
        void RebaseOntoUpstream(git_repository* repo, Tracking const& t, git_signature const* signature)
        {
            git_annotated_commit* rawBranch = nullptr;
            git_annotated_commit* rawUpstream = nullptr;
            Check("annotated HEAD", git_annotated_commit_from_ref(&rawBranch, repo, t.head.get()));
            AnnotatedCommit branch(rawBranch);
            Check("annotated upstream", git_annotated_commit_from_ref(&rawUpstream, repo, t.upstream.get()));
            AnnotatedCommit upstream(rawUpstream);

            git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
            git_rebase* raw = nullptr;
            if (int result = git_rebase_init(&raw, repo, branch.get(), upstream.get(), nullptr, &options); result < 0)
            {
                AbortRebase(repo, nullptr);
                Check("start rebase", result);
            }
            Rebase rebase(raw);
            std::optional<std::vector<std::string>> conflicts;
            try
            {
                git_index* rawIndex = nullptr;
                Check("index", git_repository_index(&rawIndex, repo));
                Index index(rawIndex);
                git_rebase_operation* operation = nullptr;
                int result = 0;
                while ((result = git_rebase_next(&operation, rebase.get())) == 0)
                {
                    if (git_index_has_conflicts(index.get()))
                    {
                        conflicts = ConflictPaths(index.get());
                        break;
                    }
                    git_oid id{};
                    result = git_rebase_commit(&id, rebase.get(), nullptr, signature, nullptr, nullptr);
                    if (result != GIT_EAPPLIED) // already upstream: nothing to commit
                    {
                        Check("rebase commit", result);
                    }
                }
                if (!conflicts)
                {
                    if (result == GIT_ECONFLICT || result == GIT_EMERGECONFLICT)
                    {
                        Log(std::format("git: rebase stopped ({})", git_error_last() ? git_error_last()->message : "conflict"));
                        conflicts = std::vector<std::string>{};
                    }
                    else if (result != GIT_ITEROVER)
                    {
                        Check("rebase next", result);
                    }
                }
                if (!conflicts)
                {
                    Check("finish rebase", git_rebase_finish(rebase.get(), signature));
                }
            }
            catch (...)
            {
                AbortRebase(repo, rebase.get());
                throw;
            }
            if (conflicts)
            {
                Check("abort rebase", git_rebase_abort(rebase.get()));
                ThrowConflict(*conflicts);
            }
        }

        void FastForward(git_repository* repo, Tracking const& t)
        {
            git_object* raw = nullptr;
            Check("look up upstream commit", git_object_lookup(&raw, repo, &t.remote, GIT_OBJECT_COMMIT));
            Object target(raw);
            git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
            checkout.checkout_strategy = GIT_CHECKOUT_SAFE;
            Check("fast-forward checkout", git_checkout_tree(repo, target.get(), &checkout));
            git_reference* moved = nullptr;
            Check("fast-forward", git_reference_set_target(&moved, t.head.get(), &t.remote, "merge: Fast-forward"));
            Reference owned(moved);
        }

        // The files the upstream commits changed: from the merge base of `before` and the upstream tip to that tip.
        std::vector<path> ChangedFiles(git_repository* repo, git_oid const& before, git_oid const& upstreamTip)
        {
            Tree baseTree;
            git_oid base{};
            int found = git_merge_base(&base, repo, &before, &upstreamTip);
            if (found != GIT_ENOTFOUND)
            {
                Check("merge base", found);
                baseTree = TreeOf(LookupCommit(repo, &base).get());
            }
            Tree upstreamTree = TreeOf(LookupCommit(repo, &upstreamTip).get());
            git_diff* raw = nullptr;
            Check("diff", git_diff_tree_to_tree(&raw, repo, baseTree.get(), upstreamTree.get(), nullptr));
            Diff diff(raw);
            std::vector<path> files;
            for (size_t i = 0, n = git_diff_num_deltas(diff.get()); i < n; i++)
            {
                files.push_back(FullPath(repo, git_diff_get_delta(diff.get(), i)->new_file.path));
            }
            return files;
        }
    }

    SyncConflict::SyncConflict(std::string const& message) : Error(message, E_ABORT) {}

    Moment Moment::Now()
    {
        SYSTEMTIME utc{}, local{};
        GetSystemTime(&utc);
        if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        {
            ThrowLastError("SystemTimeToTzSpecificLocalTime");
        }
        FILETIME utcFile{}, localFile{};
        if (!SystemTimeToFileTime(&utc, &utcFile) || !SystemTimeToFileTime(&local, &localFile))
        {
            ThrowLastError("SystemTimeToFileTime");
        }
        auto ticks = [](FILETIME const& f) { return static_cast<std::int64_t>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime); };
        constexpr std::int64_t TicksPerSecond = 10'000'000, UnixEpoch = 116'444'736'000'000'000; // 1970 in FILETIME ticks
        Moment now;
        now.utc = std::chrono::sys_seconds(std::chrono::seconds((ticks(utcFile) - UnixEpoch) / TicksPerSecond));
        now.offset = std::chrono::minutes((ticks(localFile) - ticks(utcFile)) / (60 * TicksPerSecond));
        return now;
    }

    std::optional<path> GitSync::FindRoot(path const& p)
    {
        Library library;
        Buffer gitDir;
        int found = git_repository_discover(&gitDir.buf, ToUtf8(Paths::Full(p).native()).c_str(), 0, nullptr);
        if (found == GIT_ENOTFOUND)
        {
            return std::nullopt;
        }
        Check(std::format("find repository of {}", ToUtf8(p.native())), found);
        Repository repo = Open(FromUtf8(gitDir.Text()));
        char const* workdir = git_repository_workdir(repo.get());
        if (!workdir)
        {
            return std::nullopt;
        }
        return Paths::WithoutTrailingSeparator(FromUtf8(workdir));
    }

    StatusMap GitSync::GetStatus(path const& root)
    {
        Library library;
        Repository repo = Open(root.native());
        StatusList list = Status(repo.get(), GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX);
        StatusMap states;
        for (size_t i = 0, n = git_status_list_entrycount(list.get()); i < n; i++)
        {
            git_status_entry const* entry = git_status_byindex(list.get(), i);
            FileState state = StateOf(entry->status);
            git_diff_delta const* delta = entry->head_to_index ? entry->head_to_index : entry->index_to_workdir;
            if (state != FileState::Unchanged && delta)
            {
                states[FullPath(repo.get(), delta->new_file.path).native()] = state;
            }
        }
        return states;
    }

    StatusMap GitSync::WithFolders(StatusMap const& status, path const& root)
    {
        StatusMap all = status;
        std::wstring top = Paths::WithoutTrailingSeparator(root).native();
        for (auto const& [file, fileState] : status)
        {
            // Folders: modified > new; a rename or deletion in them shows as modified.
            FileState state = fileState == FileState::Renamed || fileState == FileState::Deleted ? FileState::Modified : fileState;
            for (path dir = path(file).parent_path();
                dir.native().size() > top.size() && Paths::IsAtOrBelow(dir, top);
                dir = dir.parent_path())
            {
                auto [it, added] = all.try_emplace(dir.native(), state);
                if (!added && it->second < state)
                {
                    it->second = state;
                }
            }
        }
        return all;
    }

    SyncResult GitSync::Sync(path const& root, CredentialProvider const& credentials, std::wstring const& machineName, Moment now)
    {
        std::lock_guard network(upstream::NetworkLock());
        Library library;
        Repository repo = Open(root.native());
        Track(repo.get()); // no upstream branch: nothing is committed
        Signature signature = MakeSignature(repo.get(), now);

        SyncResult result;
        if (IsDirty(repo.get()))
        {
            std::string message = std::format("Sync from {} ({:%Y-%m-%d %H:%M})\n", ToUtf8(machineName), now.utc + now.offset);
            CommitAll(repo.get(), signature.get(), message);
            result.committed = true;
        }

        Remote remote = upstream::RemoteOf(repo.get(), Head(repo.get()).get());
        upstream::Fetch(remote.get(), credentials);

        Tracking tracking = Track(repo.get());
        git_oid before = tracking.local;
        if (tracking.behind > 0 && tracking.ahead > 0)
        {
            RebaseOntoUpstream(repo.get(), tracking, signature.get());
        }
        else if (tracking.behind > 0)
        {
            FastForward(repo.get(), tracking);
        }

        tracking = Track(repo.get());
        if (!git_oid_equal(&tracking.local, &before))
        {
            result.pulledFiles = ChangedFiles(repo.get(), before, tracking.remote);
        }
        if (tracking.ahead > 0)
        {
            upstream::Push(repo.get(), remote.get(), tracking.head.get(), credentials);
            result.pushed = true;
        }
        return result;
    }
}
