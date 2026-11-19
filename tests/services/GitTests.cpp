// Ported from XournalStudio.Tests/ExplorerTests.cs: git status with folders, discarding one file, sync (commit, push,
// pull, rebase, conflict) between two clones of a local bare repository; plus the Credential Manager names.

#include "TestSupport.h"

#include "xournal/git/GitCredentials.h"
#include "xournal/git/GitSync.h"

#include <git2.h>
#include <gtest/gtest.h>

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

        GitSync::Discard(a, a / L"start.xopp");

        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        EXPECT_EQ("new", ReadText(a / L"other.xopp")); // nothing else touched
        EXPECT_FALSE(GitSync::GetStatus(a).contains((a / L"start.xopp").native()));
        // Letter case on disk differs from the commit (Windows ignores it, git trees do not).
        WriteBytes(a / L"start.xopp", "changed again");
        GitSync::Discard(a, a / L"START.xopp");
        EXPECT_EQ("start", ReadText(a / L"start.xopp"));
        // A file the last commit does not have cannot go back.
        EXPECT_THROW(GitSync::Discard(a, a / L"other.xopp"), Error);
        EXPECT_TRUE(std::filesystem::exists(a / L"other.xopp"));
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
