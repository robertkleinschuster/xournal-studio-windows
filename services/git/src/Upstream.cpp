#include "Upstream.h"

#include "xournal/base/Log.h"

#include <windows.h>

#include <format>

namespace xournal::git
{
    using namespace git2;

    namespace
    {
        // What the libgit2 callbacks of one fetch or push report back.
        struct Callbacks
        {
            CredentialProvider const* credentials = nullptr;
            std::function<bool()> const* cancelled = nullptr;
            bool stopped = false; // a progress callback stopped the transfer because `cancelled` said so
            std::exception_ptr error;
            std::string rejected; // a reference the remote refused to update, with its reason
        };

        int AcquireCredential(git_credential** out, char const* url, char const* userName, unsigned int allowed, void* payload)
        {
            auto& callbacks = *static_cast<Callbacks*>(payload);
            if ((allowed & GIT_CREDENTIAL_USERPASS_PLAINTEXT) == 0)
            {
                return GIT_PASSTHROUGH;
            }
            try
            {
                Credential credential = (*callbacks.credentials)(url, userName ? userName : "");
                return git_credential_userpass_plaintext_new(out, credential.userName.c_str(), credential.password.c_str());
            }
            catch (...)
            {
                callbacks.error = std::current_exception();
                return GIT_EUSER;
            }
        }

        int PushUpdateReference(char const* refName, char const* status, void* payload)
        {
            if (status)
            {
                auto& callbacks = *static_cast<Callbacks*>(payload);
                callbacks.rejected += std::format("{}{}: {}", callbacks.rejected.empty() ? "" : ", ", refName, status);
            }
            return 0;
        }

        // libgit2's documented way to cancel a transfer: a progress callback returns non-zero.
        int Progress(Callbacks& callbacks)
        {
            try
            {
                if (callbacks.cancelled && *callbacks.cancelled && (*callbacks.cancelled)())
                {
                    callbacks.stopped = true;
                    return GIT_EUSER;
                }
                return 0;
            }
            catch (...)
            {
                callbacks.error = std::current_exception();
                return GIT_EUSER;
            }
        }

        int TransferProgress(git_indexer_progress const*, void* payload)
        {
            return Progress(*static_cast<Callbacks*>(payload));
        }

        int SidebandProgress(char const*, int, void* payload)
        {
            return Progress(*static_cast<Callbacks*>(payload));
        }

        void SetUp(git_remote_callbacks& remoteCallbacks, Callbacks& callbacks)
        {
            remoteCallbacks.payload = &callbacks;
            if (callbacks.credentials && *callbacks.credentials)
            {
                remoteCallbacks.credentials = AcquireCredential;
            }
            if (callbacks.cancelled && *callbacks.cancelled)
            {
                remoteCallbacks.transfer_progress = TransferProgress;
                remoteCallbacks.sideband_progress = SidebandProgress;
            }
        }

        // A libgit2 call that may have failed inside a callback: the callback's own error is the real one.
        void CheckWithCallbacks(std::string_view step, int result, Callbacks const& callbacks)
        {
            if (result < 0 && callbacks.error)
            {
                LogException(std::format("git {}", step), callbacks.error);
                std::rethrow_exception(callbacks.error);
            }
            if (result < 0 && callbacks.stopped)
            {
                Log(std::format("git {}: cancelled", step)); // asked for, not a failure
                throw Error(std::format("git {}: cancelled", step), E_ABORT);
            }
            Check(step, result);
        }

        // HEAD when it is a branch with an upstream branch; nothing for an unborn or detached HEAD or no upstream.
        std::optional<std::pair<Reference, Reference>> BranchAndUpstream(git_repository* repo)
        {
            git_reference* rawHead = nullptr;
            int result = git_repository_head(&rawHead, repo);
            if (result == GIT_EUNBORNBRANCH || result == GIT_ENOTFOUND)
            {
                return std::nullopt;
            }
            Check("HEAD", result);
            Reference head(rawHead);
            if (!git_reference_is_branch(head.get()))
            {
                return std::nullopt; // detached
            }
            git_reference* rawUpstream = nullptr;
            result = git_branch_upstream(&rawUpstream, head.get());
            if (result == GIT_ENOTFOUND)
            {
                return std::nullopt;
            }
            Check("upstream branch", result);
            return std::pair(std::move(head), Reference(rawUpstream));
        }
    }

    std::mutex& upstream::NetworkLock()
    {
        static std::mutex lock;
        return lock;
    }

    Remote upstream::RemoteOf(git_repository* repo, git_reference* head)
    {
        Buffer remoteName;
        Check("remote of upstream", git_branch_upstream_remote(&remoteName.buf, repo, git_reference_name(head)));
        git_remote* remote = nullptr;
        Check("look up remote", git_remote_lookup(&remote, repo, remoteName.Text().c_str()));
        return Remote(remote);
    }

    void upstream::Fetch(git_remote* remote, CredentialProvider const& credentials, std::function<bool()> const& cancelled)
    {
        Callbacks callbacks{ &credentials, &cancelled };
        git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
        SetUp(options.callbacks, callbacks);
        CheckWithCallbacks("fetch", git_remote_fetch(remote, nullptr, &options, nullptr), callbacks);
    }

    std::string upstream::Push(git_repository* repo, git_remote* remote, git_reference* head, CredentialProvider const& credentials)
    {
        Buffer merge;
        Check("upstream of branch", git_branch_upstream_merge(&merge.buf, repo, git_reference_name(head)));
        std::string spec = std::format("{}:{}", git_reference_name(head), merge.Text());
        char* specs[] = { spec.data() };
        git_strarray refspecs{ specs, 1 };
        Callbacks callbacks{ &credentials };
        git_push_options options = GIT_PUSH_OPTIONS_INIT;
        SetUp(options.callbacks, callbacks);
        options.callbacks.push_update_reference = PushUpdateReference;
        CheckWithCallbacks("push", git_remote_push(remote, &refspecs, &options), callbacks);
        if (!callbacks.rejected.empty())
        {
            Log(std::format("git push rejected: {}", callbacks.rejected)); // the caller decides: retry or throw
        }
        return callbacks.rejected;
    }

    std::optional<Divergence> GitSync::GetDivergence(path const& root)
    {
        Library library;
        Repository repo = Open(root.native());
        auto branch = BranchAndUpstream(repo.get());
        if (!branch)
        {
            return std::nullopt;
        }
        git_oid const* local = git_reference_target(branch->first.get());
        git_oid const* remote = git_reference_target(branch->second.get());
        if (!local || !remote)
        {
            return std::nullopt; // symbolic references: nothing to count
        }
        Divergence divergence;
        Check("ahead/behind", git_graph_ahead_behind(&divergence.ahead, &divergence.behind, repo.get(), local, remote));
        return divergence;
    }

    bool GitSync::Fetch(path const& root, CredentialProvider const& credentials, std::function<bool()> const& cancelled)
    {
        std::lock_guard network(upstream::NetworkLock());
        Library library;
        Repository repo = Open(root.native());
        auto branch = BranchAndUpstream(repo.get());
        if (!branch)
        {
            return false;
        }
        Remote remote = upstream::RemoteOf(repo.get(), branch->first.get());
        upstream::Fetch(remote.get(), credentials, cancelled);
        return true;
    }
}
