// Ported from XournalStudio.Tests/ExplorerTests.cs: git status with folders, discarding one file, sync (commit, push,
// pull, rebase, conflict) between two clones of a local bare repository; plus the Credential Manager names.

#include "TestSupport.h"

#include "xournal/git/GitCredentials.h"
#include "xournal/git/GitSync.h"
#include "xournal/git/SyncStatus.h"

#include <git2.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <format>

namespace xournal::tests
{
    using namespace xournal::git;
    using std::filesystem::path;

    namespace
    {
        // 2026-11-15 16:30 UTC.
        Moment const Now{ std::chrono::sys_days(std::chrono::year(2026) / 11 / 15) + std::chrono::hours(16) + std::chrono::minutes(30),
            std::chrono::minutes(0) };

        void Ok(int result)
        {
            if (result < 0)
            {
                git_error const* e = git_error_last();
                throw std::runtime_error(std::format("libgit2: {} ({})", e && e->message ? e->message : "?", result));
            }
        }

        std::string Utf8(path const& p) { return ToUtf8(p.native()); }

        // Repositories set up with libgit2 itself (as the C# tests used LibGit2Sharp).
        class Repos : public testing::Test
        {
        protected:
            void SetUp() override { git_libgit2_init(); }
            void TearDown() override { git_libgit2_shutdown(); }

            path Clone(path const& remote, std::wstring const& name)
            {
                path target = temp / name;
                git_repository* repo = nullptr;
                Ok(git_clone(&repo, Utf8(remote).c_str(), Utf8(target).c_str(), nullptr));
                git_config* config = nullptr;
                Ok(git_repository_config(&config, repo));
                git_config* local = nullptr;
                Ok(git_config_open_level(&local, config, GIT_CONFIG_LEVEL_LOCAL));
                Ok(git_config_set_string(local, "user.name", "Test"));
                Ok(git_config_set_string(local, "user.email", "test@example.com"));
                git_config_free(local);
                git_config_free(config);
                git_repository_free(repo);
                return target;
            }

            // A bare remote with one commit ("start.xopp" = "start") and two clones that track it.
            std::pair<path, path> TwoClones()
            {
                path remote = temp / L"remote.git";
                git_repository* bare = nullptr;
                Ok(git_repository_init(&bare, Utf8(remote).c_str(), 1));
                git_repository_free(bare);
                path a = Clone(remote, L"a");
                WriteBytes(a / L"start.xopp", "start");

                git_repository* repo = nullptr;
                Ok(git_repository_open(&repo, Utf8(a).c_str()));
                git_index* index = nullptr;
                Ok(git_repository_index(&index, repo));
                Ok(git_index_add_bypath(index, "start.xopp"));
                Ok(git_index_write(index));
                git_oid treeId{}, commitId{};
                Ok(git_index_write_tree(&treeId, index));
                git_tree* tree = nullptr;
                Ok(git_tree_lookup(&tree, repo, &treeId));
                git_signature* sig = nullptr;
                Ok(git_signature_now(&sig, "Test", "test@example.com"));
                Ok(git_commit_create(&commitId, repo, "HEAD", sig, sig, nullptr, "start\n", tree, 0, nullptr));
                git_reference* head = nullptr;
                Ok(git_repository_head(&head, repo)); // master or init.defaultBranch
                std::string name = git_reference_name(head);
                std::string spec = name + ":" + name;
                char* specs[] = { spec.data() };
                git_strarray refspecs{ specs, 1 };
                git_remote* origin = nullptr;
                Ok(git_remote_lookup(&origin, repo, "origin"));
                Ok(git_remote_push(origin, &refspecs, nullptr));
                Ok(git_branch_set_upstream(head, ("origin/" + std::string(git_reference_shorthand(head))).c_str()));
                git_remote_free(origin);
                git_reference_free(head);
                git_signature_free(sig);
                git_tree_free(tree);
                git_index_free(index);
                git_repository_free(repo);
                return { a, Clone(remote, L"b") };
            }

            void Stage(path const& root, char const* file)
            {
                git_repository* repo = nullptr;
                Ok(git_repository_open(&repo, Utf8(root).c_str()));
                git_index* index = nullptr;
                Ok(git_repository_index(&index, repo));
                Ok(git_index_add_bypath(index, file));
                Ok(git_index_write(index));
                git_index_free(index);
                git_repository_free(repo);
            }

            // A local commit of everything staged, not pushed.
            void CommitStaged(path const& root, char const* message)
            {
                git_repository* repo = nullptr;
                Ok(git_repository_open(&repo, Utf8(root).c_str()));
                git_index* index = nullptr;
                Ok(git_repository_index(&index, repo));
                git_oid treeId{}, headId{}, commitId{};
                Ok(git_index_write_tree(&treeId, index));
                git_tree* tree = nullptr;
                Ok(git_tree_lookup(&tree, repo, &treeId));
                Ok(git_reference_name_to_id(&headId, repo, "HEAD"));
                git_commit* parent = nullptr;
                Ok(git_commit_lookup(&parent, repo, &headId));
                git_signature* sig = nullptr;
                Ok(git_signature_now(&sig, "Test", "test@example.com"));
                git_commit const* parents[] = { parent };
                Ok(git_commit_create(&commitId, repo, "HEAD", sig, sig, nullptr, message, tree, 1, parents));
                git_signature_free(sig);
                git_commit_free(parent);
                git_tree_free(tree);
                git_index_free(index);
                git_repository_free(repo);
            }

            struct HeadInfo
            {
                std::string summary;
                unsigned parents = 0;
                size_t ahead = 0;
                int state = 0;
            };

            HeadInfo Head(path const& root)
            {
                HeadInfo info;
                git_repository* repo = nullptr;
                Ok(git_repository_open(&repo, Utf8(root).c_str()));
                git_reference* head = nullptr;
                Ok(git_repository_head(&head, repo));
                git_commit* commit = nullptr;
                Ok(git_commit_lookup(&commit, repo, git_reference_target(head)));
                info.summary = git_commit_summary(commit);
                info.parents = git_commit_parentcount(commit);
                git_reference* upstream = nullptr;
                Ok(git_branch_upstream(&upstream, head));
                size_t behind = 0;
                Ok(git_graph_ahead_behind(&info.ahead, &behind, repo, git_reference_target(head), git_reference_target(upstream)));
                info.state = git_repository_state(repo);
                git_reference_free(upstream);
                git_commit_free(commit);
                git_reference_free(head);
                git_repository_free(repo);
                return info;
            }

            TempFolder temp;
        };
    }

    TEST(GitSync, FolderStatusFollowsTheFilesBelow)
    {
        path root = L"C:\\repo";
        StatusMap status{
            { (root / L"A" / L"B" / L"neu.xopp").native(), FileState::New },
            { (root / L"A" / L"alt.xopp").native(), FileState::Modified },
            { (root / L"C" / L"neu.pdf").native(), FileState::New },
            { (root / L"D" / L"umbenannt.xopp").native(), FileState::Renamed },
            { (root / L"top.xopp").native(), FileState::Modified },
        };

        StatusMap all = GitSync::WithFolders(status, root);

        EXPECT_EQ(FileState::Modified, all.at((root / L"A").native()));
        EXPECT_EQ(FileState::New, all.at((root / L"A" / L"B").native()));
        EXPECT_EQ(FileState::New, all.at((root / L"c").native())); // case-insensitive like the file system
        EXPECT_EQ(FileState::Modified, all.at((root / L"D").native())); // a rename shows its folder as modified
        EXPECT_FALSE(all.contains(root.native()));
        EXPECT_EQ(FileState::New, all.at((root / L"A" / L"B" / L"neu.xopp").native()));
    }

    TEST_F(Repos, DeletedFileMarksItsFolderModified)
    {
        auto [a, b] = TwoClones();
        std::filesystem::create_directories(a / L"UE");
        WriteBytes(a / L"UE" / L"weg.xopp", "x");
        GitSync::Sync(a, {}, L"PC-A", Now);
        std::filesystem::remove(a / L"UE" / L"weg.xopp");
        WriteBytes(a / L"UE" / L"bleibt.xopp", "x");

        StatusMap all = GitSync::WithFolders(GitSync::GetStatus(a), a);

        EXPECT_EQ(FileState::Modified, all.at((a / L"UE").native()));
    }

    TEST_F(Repos, StatusReportsNewAndModified)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"start.xopp", "changed");
        WriteBytes(a / L"neu.xopp", "new");

        StatusMap status = GitSync::GetStatus(a);

        EXPECT_EQ(FileState::Modified, status.at((a / L"start.xopp").native()));
        EXPECT_EQ(FileState::New, status.at((a / L"neu.xopp").native()));
        EXPECT_EQ(a, GitSync::FindRoot(a / L"start.xopp"));
        EXPECT_EQ(std::nullopt, GitSync::FindRoot(temp.Path()));
    }

    TEST_F(Repos, DiscardGivesOneFileItsCommittedContentBack)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"start.xopp", "changed");
        WriteBytes(a / L"other.xopp", "new");
        Stage(a, "start.xopp"); // staged changes go too

        DiscardPlan plan = GitSync::PlanDiscard(a, a / L"start.xopp");
        EXPECT_EQ(std::vector<path>{ a / L"start.xopp" }, plan.restore);
        EXPECT_TRUE(plan.remove.empty());
        GitSync::Discard(a, plan);

        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        EXPECT_EQ("new", ReadText(a / L"other.xopp")); // nothing else touched
        EXPECT_FALSE(GitSync::GetStatus(a).contains((a / L"start.xopp").native()));
        // Letter case on disk differs from the commit (Windows ignores it, git trees do not).
        WriteBytes(a / L"start.xopp", "changed again");
        GitSync::Discard(a, GitSync::PlanDiscard(a, a / L"START.xopp"));
        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        // A file the last commit does not have has nothing to go back to: it is to be removed (by the app).
        plan = GitSync::PlanDiscard(a, a / L"other.xopp");
        EXPECT_TRUE(plan.restore.empty());
        EXPECT_EQ(std::vector<path>{ a / L"other.xopp" }, plan.remove);
        GitSync::Discard(a, plan);
        EXPECT_TRUE(std::filesystem::exists(a / L"other.xopp")); // the git part leaves the file alone
        EXPECT_TRUE(GitSync::PlanDiscard(a, a / L"nothing.xopp").Empty());
    }

    TEST_F(Repos, DeletedFilesAreShownAndComeBack)
    {
        auto [a, b] = TwoClones();
        std::filesystem::create_directories(a / L"UE");
        WriteBytes(a / L"UE" / L"blatt.xopp", "blatt");
        WriteBytes(a / L"UE" / L"weg.xopp", "weg");
        GitSync::Sync(a, {}, L"PC-A", Now);
        std::filesystem::remove(a / L"UE" / L"weg.xopp");      // deleted in the working tree
        std::filesystem::remove(a / L"start.xopp");
        git_repository* repo = nullptr;                         // and one staged as deleted
        Ok(git_repository_open(&repo, Utf8(a).c_str()));
        git_index* index = nullptr;
        Ok(git_repository_index(&index, repo));
        Ok(git_index_remove_bypath(index, "start.xopp"));
        Ok(git_index_write(index));
        git_index_free(index);
        git_repository_free(repo);

        StatusMap status = GitSync::GetStatus(a);
        EXPECT_EQ(FileState::Deleted, status.at((a / L"UE" / L"weg.xopp").native()));
        EXPECT_EQ(FileState::Deleted, status.at((a / L"start.xopp").native()));
        EXPECT_EQ(FileState::Modified, GitSync::WithFolders(status, a).at((a / L"UE").native()));

        GitSync::Discard(a, GitSync::PlanDiscard(a, a / L"UE" / L"weg.xopp"));
        GitSync::Discard(a, GitSync::PlanDiscard(a, a / L"start.xopp"));

        EXPECT_EQ("weg", ReadText(a / L"UE" / L"weg.xopp"));
        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        EXPECT_TRUE(GitSync::GetStatus(a).empty());
    }

    TEST_F(Repos, DiscardingAFolderCoversEverythingInIt)
    {
        auto [a, b] = TwoClones();
        std::filesystem::create_directories(a / L"UE" / L"Teil");
        WriteBytes(a / L"UE" / L"blatt.xopp", "blatt");
        WriteBytes(a / L"UE" / L"Teil" / L"weg.xopp", "weg");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(a / L"UE" / L"blatt.xopp", "changed");
        std::filesystem::remove(a / L"UE" / L"Teil" / L"weg.xopp");
        WriteBytes(a / L"UE" / L"neu.xopp", "new");
        WriteBytes(a / L"UE" / L"added.xopp", "added");
        Stage(a, "UE/added.xopp");
        WriteBytes(a / L"start.xopp", "outside"); // not in the folder
        WriteBytes(a / L"UEbung.xopp", "a sibling whose name starts like the folder");

        DiscardPlan plan = GitSync::PlanDiscard(a, a / L"ue");
        std::ranges::sort(plan.restore);
        std::ranges::sort(plan.remove);
        EXPECT_EQ((std::vector<path>{ a / L"UE" / L"Teil" / L"weg.xopp", a / L"UE" / L"blatt.xopp" }), plan.restore);
        EXPECT_EQ((std::vector<path>{ a / L"UE" / L"added.xopp", a / L"UE" / L"neu.xopp" }), plan.remove);
        EXPECT_EQ(std::vector<path>{ a / L"UE" / L"blatt.xopp" }, plan.overwritten); // the deleted one is not on disk
        EXPECT_TRUE(plan.unstage.empty());

        GitSync::Discard(a, plan);

        EXPECT_EQ("blatt", ReadText(a / L"UE" / L"blatt.xopp"));
        EXPECT_EQ("weg", ReadText(a / L"UE" / L"Teil" / L"weg.xopp"));
        EXPECT_EQ("outside", ReadText(a / L"start.xopp"));
        StatusMap status = GitSync::GetStatus(a);
        EXPECT_EQ(FileState::New, status.at((a / L"UE" / L"added.xopp").native())); // unstaged: untracked now
        EXPECT_EQ(FileState::New, status.at((a / L"UE" / L"neu.xopp").native()));
        EXPECT_EQ(4u, status.size());
    }

    TEST_F(Repos, AStagedFileDeletedOnDiskIsOnlyUnstaged)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"added.xopp", "added");
        Stage(a, "added.xopp");
        std::filesystem::remove(a / L"added.xopp");

        DiscardPlan plan = GitSync::PlanDiscard(a, a);
        EXPECT_TRUE(plan.remove.empty()); // nothing on disk to recycle
        EXPECT_EQ(std::vector<path>{ a / L"added.xopp" }, plan.unstage);
        GitSync::Discard(a, plan);

        EXPECT_TRUE(GitSync::GetStatus(a).empty());
    }

    TEST_F(Repos, DiscardingARenameRestoresTheOldPath)
    {
        auto [a, b] = TwoClones();
        std::filesystem::rename(a / L"start.xopp", a / L"neu.xopp"); // renamed and staged, as git mv does
        git_repository* repo = nullptr;
        Ok(git_repository_open(&repo, Utf8(a).c_str()));
        git_index* index = nullptr;
        Ok(git_repository_index(&index, repo));
        Ok(git_index_remove_bypath(index, "start.xopp"));
        Ok(git_index_add_bypath(index, "neu.xopp"));
        Ok(git_index_write(index));
        git_index_free(index);
        git_repository_free(repo);
        ASSERT_EQ(FileState::Renamed, GitSync::GetStatus(a).at((a / L"neu.xopp").native()));

        DiscardPlan plan = GitSync::PlanDiscard(a, a / L"neu.xopp");
        EXPECT_EQ(std::vector<path>{ a / L"start.xopp" }, plan.restore);
        EXPECT_TRUE(plan.overwritten.empty());
        EXPECT_EQ(std::vector<path>{ a / L"neu.xopp" }, plan.remove);
        GitSync::Discard(a, plan);

        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        StatusMap status = GitSync::GetStatus(a);
        EXPECT_EQ(1u, status.size()); // the new name is untracked now; the app recycles it
        EXPECT_EQ(FileState::New, status.at((a / L"neu.xopp").native()));
    }

    TEST_F(Repos, AFetchCanBeCancelled)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"a.xopp", "a");
        GitSync::Sync(a, {}, L"PC-A", Now); // something to transfer

        try
        {
            GitSync::Fetch(b, {}, [] { return true; });
            FAIL() << "the fetch was not cancelled";
        }
        catch (Error const& e)
        {
            EXPECT_EQ(E_ABORT, e.Code());
        }
        EXPECT_EQ((Divergence{ 0, 0 }), GitSync::GetDivergence(b)); // the references stay as they were
        EXPECT_TRUE(GitSync::Fetch(b, {}, [] { return false; }));
        EXPECT_EQ((Divergence{ 0, 1 }), GitSync::GetDivergence(b));
    }

    TEST_F(Repos, SyncCommitsPushesAndPulls)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"start.xopp", "from a");

        SyncResult pushedFromA = GitSync::Sync(a, {}, L"PC-A", Now);
        SyncResult pulledToB = GitSync::Sync(b, {}, L"PC-B", Now);

        EXPECT_TRUE(pushedFromA.committed);
        EXPECT_TRUE(pushedFromA.pushed);
        EXPECT_FALSE(pulledToB.committed);
        EXPECT_FALSE(pulledToB.pushed);
        EXPECT_EQ(std::vector<path>{ b / L"start.xopp" }, pulledToB.pulledFiles);
        EXPECT_EQ("from a", ReadText(b / L"start.xopp"));
        EXPECT_EQ("Sync from PC-A (2026-11-15 16:30)", Head(b).summary);
    }

    TEST_F(Repos, SyncRebasesOwnCommitsOntoRemote)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"a.xopp", "a");
        WriteBytes(b / L"b.xopp", "b");

        GitSync::Sync(a, {}, L"PC-A", Now);
        SyncResult result = GitSync::Sync(b, {}, L"PC-B", Now);

        EXPECT_TRUE(result.pushed);
        EXPECT_EQ(std::vector<path>{ b / L"a.xopp" }, result.pulledFiles);
        HeadInfo head = Head(b);
        EXPECT_EQ("Sync from PC-B (2026-11-15 16:30)", head.summary);
        EXPECT_EQ(1u, head.parents); // rebased, no merge commit
        EXPECT_EQ(0u, head.ahead);
        EXPECT_EQ("a", ReadText(b / L"a.xopp"));
    }

    TEST_F(Repos, ConflictIsAbortedAndReported)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"start.xopp", "a");
        WriteBytes(b / L"start.xopp", "b");
        GitSync::Sync(a, {}, L"PC-A", Now);

        try
        {
            GitSync::Sync(b, {}, L"PC-B", Now);
            FAIL() << "no conflict reported";
        }
        catch (SyncConflict const& e)
        {
            EXPECT_NE(std::string(e.what()).find("start.xopp"), std::string::npos) << e.what();
            EXPECT_EQ(E_ABORT, e.Code());
        }
        EXPECT_EQ("b", ReadText(b / L"start.xopp"));
        EXPECT_EQ(GIT_REPOSITORY_STATE_NONE, Head(b).state);
    }

    TEST_F(Repos, SyncBringsInSeveralForeignCommitsAndPushesOnTop)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"one.xopp", "1");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(a / L"two.xopp", "2");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(a / L"start.xopp", "changed by a");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(b / L"mine.xopp", "mine");

        SyncResult result = GitSync::Sync(b, {}, L"PC-B", Now);

        EXPECT_TRUE(result.committed);
        EXPECT_TRUE(result.pushed);
        EXPECT_EQ(3u, result.pulledFiles.size()); // every file the foreign commits changed, once
        EXPECT_EQ("changed by a", ReadText(b / L"start.xopp"));
        EXPECT_EQ("mine", ReadText(b / L"mine.xopp"));
        HeadInfo head = Head(b);
        EXPECT_EQ(0u, head.ahead);
        EXPECT_EQ(GIT_REPOSITORY_STATE_NONE, head.state);
        // a gets b's file on its next sync: the pushed history is a straight line on top of a's commits
        SyncResult back = GitSync::Sync(a, {}, L"PC-A", Now);
        EXPECT_EQ(std::vector<path>{ a / L"mine.xopp" }, back.pulledFiles);
    }

    TEST_F(Repos, ARepeatedConflictKeepsTheRepositoryCleanAndTheLocalWork)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"start.xopp", "a");
        GitSync::Sync(a, {}, L"PC-A", Now);

        for (int round = 1; round <= 3; ++round)
        {
            WriteBytes(b / L"start.xopp", std::format("b{}", round));
            WriteBytes(b / std::format(L"own{}.xopp", round), "own");
            EXPECT_THROW(GitSync::Sync(b, {}, L"PC-B", Now), SyncConflict) << "round " << round;
            EXPECT_EQ(GIT_REPOSITORY_STATE_NONE, Head(b).state) << "round " << round;
            EXPECT_EQ(std::format("b{}", round), ReadText(b / L"start.xopp"));
            EXPECT_EQ("own", ReadText(b / std::format(L"own{}.xopp", round)));
        }
        EXPECT_EQ(3u, Head(b).ahead); // the local commits stay, nothing was pushed
    }

    TEST_F(Repos, PullBringsTheServersCommitsInAndNeverUploadsOrCommits)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"new.xopp", "from a");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(b / L"note.txt", "uncommitted"); // not touched by the pull

        SyncResult result = GitSync::Pull(b, {}, Now);

        EXPECT_EQ(std::vector<path>{ b / L"new.xopp" }, result.pulledFiles);
        EXPECT_FALSE(result.committed);
        EXPECT_FALSE(result.pushed);
        EXPECT_EQ("from a", ReadText(b / L"new.xopp"));
        EXPECT_EQ("uncommitted", ReadText(b / L"note.txt"));
        EXPECT_EQ("Sync from PC-A (2026-11-15 16:30)", Head(b).summary);
        EXPECT_TRUE(GitSync::Pull(b, {}, Now).pulledFiles.empty()); // nothing new: nothing happens
    }

    TEST_F(Repos, PullKeepsUncommittedChangesOfFilesTheServerDidNotTouch)
    {
        auto [a, b] = TwoClones();
        WriteBytes(a / L"new.xopp", "from a");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(b / L"start.xopp", "edited by b"); // a tracked file, uncommitted

        EXPECT_NO_THROW(GitSync::Pull(b, {}, Now)); // a fast-forward over other files
        EXPECT_EQ("edited by b", ReadText(b / L"start.xopp"));
        EXPECT_EQ("from a", ReadText(b / L"new.xopp"));
    }

    TEST_F(Repos, PullRebasesOwnCommitsButDoesNotPushThem)
    {
        auto [a, b] = TwoClones();
        WriteBytes(b / L"mine.xopp", "mine");
        Stage(b, "mine.xopp");
        CommitStaged(b, "mine\n");
        WriteBytes(a / L"theirs.xopp", "theirs");
        GitSync::Sync(a, {}, L"PC-A", Now);

        SyncResult result = GitSync::Pull(b, {}, Now);

        EXPECT_EQ(std::vector<path>{ b / L"theirs.xopp" }, result.pulledFiles);
        EXPECT_FALSE(result.pushed);
        HeadInfo head = Head(b);
        EXPECT_EQ("mine", head.summary);
        EXPECT_EQ(1u, head.ahead); // still to push
        EXPECT_EQ(GIT_REPOSITORY_STATE_NONE, head.state);
        EXPECT_EQ("theirs", ReadText(b / L"theirs.xopp"));
        GitSync::Pull(a, {}, Now);
        EXPECT_FALSE(std::filesystem::exists(a / L"mine.xopp")); // nothing of b reached the server
    }

    TEST_F(Repos, PullWithOwnCommitsAndUncommittedChangesLeavesEverythingAsItWas)
    {
        auto [a, b] = TwoClones();
        WriteBytes(b / L"mine.xopp", "mine");
        Stage(b, "mine.xopp");
        CommitStaged(b, "mine\n");
        WriteBytes(a / L"theirs.xopp", "theirs");
        GitSync::Sync(a, {}, L"PC-A", Now);
        WriteBytes(b / L"start.xopp", "edited by b"); // a rebase would need a clean tree

        EXPECT_THROW(GitSync::Pull(b, {}, Now), Error);
        EXPECT_EQ("edited by b", ReadText(b / L"start.xopp"));
        EXPECT_FALSE(std::filesystem::exists(b / L"theirs.xopp"));
        EXPECT_EQ(GIT_REPOSITORY_STATE_NONE, Head(b).state);
    }

    TEST_F(Repos, SyncWithoutUpstreamIsRefusedBeforeCommitting)
    {
        path lone = temp / L"lone";
        git_repository* repo = nullptr;
        Ok(git_repository_init(&repo, Utf8(lone).c_str(), 0));
        git_repository_free(repo);
        WriteBytes(lone / L"x.xopp", "x");

        EXPECT_THROW(GitSync::Sync(lone, {}, L"PC", Now), Error);
        EXPECT_EQ(FileState::New, GitSync::GetStatus(lone).at((lone / L"x.xopp").native()));
    }

    TEST_F(Repos, ALocalRemoteNeverAsksForCredentials)
    {
        auto [a, b] = TwoClones();
        // A local remote never asks: the provider is not called and the sync works.
        bool asked = false;
        CredentialProvider provider = [&](std::string const&, std::string const&) -> Credential
        {
            asked = true;
            throw Error("no credential", E_FAIL);
        };
        EXPECT_NO_THROW(GitSync::Sync(a, provider, L"PC-A", Now));
        EXPECT_FALSE(asked);
    }

    TEST_F(Repos, DivergenceCountsCommitsToPushAndToPullAfterAFetch)
    {
        auto [a, b] = TwoClones();
        EXPECT_EQ((Divergence{ 0, 0 }), GitSync::GetDivergence(b));

        WriteBytes(a / L"a.xopp", "a");
        GitSync::Sync(a, {}, L"PC-A", Now); // one commit on the server
        WriteBytes(b / L"b.xopp", "b");
        WriteBytes(b / L"start.xopp", "changed in b");
        EXPECT_EQ(2u, GitSync::GetStatus(b).size()); // the changed files a sync would commit

        EXPECT_EQ((Divergence{ 0, 0 }), GitSync::GetDivergence(b)); // the server's commit is not fetched yet
        EXPECT_TRUE(GitSync::Fetch(b, {}));
        EXPECT_EQ((Divergence{ 0, 1 }), GitSync::GetDivergence(b));
        // The fetch touched neither the working tree nor the branch.
        EXPECT_FALSE(std::filesystem::exists(b / L"a.xopp"));
        EXPECT_EQ("changed in b", ReadText(b / L"start.xopp"));
        EXPECT_EQ(2u, GitSync::GetStatus(b).size());
        EXPECT_EQ("start", Head(b).summary);

        // A local commit that is not pushed yet.
        git_repository* repo = nullptr;
        Ok(git_repository_open(&repo, Utf8(b).c_str()));
        git_reference* head = nullptr;
        Ok(git_repository_head(&head, repo));
        git_commit* parent = nullptr;
        Ok(git_commit_lookup(&parent, repo, git_reference_target(head)));
        git_tree* tree = nullptr;
        Ok(git_commit_tree(&tree, parent));
        git_signature* sig = nullptr;
        Ok(git_signature_now(&sig, "Test", "test@example.com"));
        git_oid id{};
        git_commit const* parents[] = { parent };
        Ok(git_commit_create(&id, repo, "HEAD", sig, sig, nullptr, "local\n", tree, 1, parents));
        git_signature_free(sig);
        git_tree_free(tree);
        git_commit_free(parent);
        git_reference_free(head);
        git_repository_free(repo);

        EXPECT_EQ((Divergence{ 1, 1 }), GitSync::GetDivergence(b));
    }

    TEST_F(Repos, NoUpstreamMeansNothingToCountOrFetch)
    {
        path lone = temp / L"lone";
        git_repository* repo = nullptr;
        Ok(git_repository_init(&repo, Utf8(lone).c_str(), 0));
        git_repository_free(repo);
        EXPECT_EQ(std::nullopt, GitSync::GetDivergence(lone)); // unborn HEAD
        EXPECT_FALSE(GitSync::Fetch(lone, {}));
    }

    TEST_F(Repos, FetchFromAMissingRemoteThrows)
    {
        auto [a, b] = TwoClones();
        std::filesystem::rename(temp / L"remote.git", temp / L"moved.git");
        EXPECT_THROW(GitSync::Fetch(b, {}), Error);
        EXPECT_EQ((Divergence{ 0, 0 }), GitSync::GetDivergence(b)); // the last known state stays
    }

    TEST(SyncStatus, ButtonShowsWhatIsPendingLikeCodeEditors)
    {
        EXPECT_EQ(L"", SyncStatus{}.CountsText());
        EXPECT_EQ(L"", (SyncStatus{ true, 0, Divergence{ 0, 0 } }.CountsText()));
        EXPECT_EQ(L"3  ↑1  ↓2", (SyncStatus{ true, 3, Divergence{ 1, 2 } }.CountsText()));
        EXPECT_EQ(L"↓2", (SyncStatus{ true, 0, Divergence{ 0, 2 } }.CountsText()));
        EXPECT_EQ(L"1  ↓?", (SyncStatus{ true, 1, Divergence{ 0, 2 }, false }.CountsText()));
        EXPECT_EQ(L"4", (SyncStatus{ true, 4, std::nullopt }.CountsText()));
    }

    TEST(SyncStatus, DetailsSayItInWords)
    {
        EXPECT_EQ(L"Alles synchronisiert.", (SyncStatus{ true, 0, Divergence{ 0, 0 } }.Details()));
        EXPECT_EQ(L"3 geänderte Dateien\n1 Commit zu senden\n2 Commits zu holen",
            (SyncStatus{ true, 3, Divergence{ 1, 2 } }.Details()));
        EXPECT_EQ(L"1 geänderte Datei\nStand des Servers unbekannt (Abruf fehlgeschlagen).",
            (SyncStatus{ true, 1, Divergence{ 0, 0 }, false }.Details()));
        EXPECT_NE(std::wstring::npos, (SyncStatus{ true, 0, std::nullopt }.Details().find(L"Kein Upstream-Zweig")));
        EXPECT_NE(std::wstring::npos, SyncStatus{}.Details().find(L"Kein Git-Repository"));
    }

    TEST(GitCredentials, TargetsAreGitForWindowsNames)
    {
        EXPECT_EQ((std::vector<std::wstring>{ L"git:https://github.com" }),
            GitCredentials::Targets("https://github.com/user/repo.git", ""));
        EXPECT_EQ((std::vector<std::wstring>{ L"git:https://robert@github.com", L"git:https://github.com" }),
            GitCredentials::Targets("https://robert@github.com/user/repo.git", "robert"));
        EXPECT_EQ((std::vector<std::wstring>{ L"git:https://git.example.org:8443" }),
            GitCredentials::Targets("https://git.example.org:8443/repo.git", ""));
        EXPECT_EQ((std::vector<std::wstring>{ L"git:http://example.org" }), GitCredentials::Targets("http://example.org:80/r", ""));
    }

    TEST(GitCredentials, SecretsAreUtf8OrUtf16)
    {
        std::string utf8 = "t\xC3\xB6ken"; // "töken" as the Git Credential Manager stores it
        EXPECT_EQ(utf8, GitCredentials::DecodeSecret({ reinterpret_cast<std::uint8_t const*>(utf8.data()), utf8.size() }));
        std::wstring utf16 = L"t\u00F6ken"; // as git's wincred helper stores it
        EXPECT_EQ(utf8, GitCredentials::DecodeSecret({ reinterpret_cast<std::uint8_t const*>(utf16.data()), utf16.size() * 2 }));
    }
}
