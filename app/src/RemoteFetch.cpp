#include "RemoteFetch.h"

#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/git/GitCredentials.h"
#include "xournal/git/GitSync.h"

#include <format>
#include <optional>

namespace xournal::app
{
    namespace
    {
        constexpr auto MinimumInterval = std::chrono::minutes(5);
    }

    RemoteFetch::~RemoteFetch()
    {
        ++*generation; // a fetch still running is not reported
        Cancel();
    }

    void RemoteFetch::Cancel()
    {
        if (cancelled) *cancelled = true;
    }

    void RemoteFetch::SetFolder(std::optional<std::filesystem::path> next)
    {
        folder = std::move(next);
        ++*generation;
        Cancel();
        fetching = false;
        lastFetch.reset();
        remoteKnown = true;
        Start();
    }

    void RemoteFetch::FetchIfDue()
    {
        if (fetching || (lastFetch && std::chrono::steady_clock::now() - *lastFetch < MinimumInterval))
        {
            return;
        }
        Start();
    }

    void RemoteFetch::SyncEnded(bool succeeded)
    {
        if (succeeded)
        {
            lastFetch = std::chrono::steady_clock::now();
            remoteKnown = true;
        }
    }

    void RemoteFetch::Start()
    {
        if (!folder)
        {
            return;
        }
        fetching = true;
        lastFetch = std::chrono::steady_clock::now();
        cancelled = std::make_shared<std::atomic<bool>>(false);
        std::uint64_t mine = *generation;
        std::weak_ptr<std::uint64_t> current = generation;
        UiQueue::RunInBackground("fetch", [this, mine, current, wanted = *folder, stop = cancelled]
        {
            std::optional<bool> known = true; // nothing: cancelled - the last state stands
            try
            {
                if (auto root = git::GitSync::FindRoot(wanted))
                {
                    if (git::GitSync::Fetch(*root, git::GitCredentials::FromCredentialManager(), [stop] { return stop->load(); }))
                    {
                        Log(std::format("fetch: {}", ToUtf8(root->native())));
                    }
                }
            }
            catch (Error const& e)
            {
                if (e.Code() == E_ABORT && stop->load())
                {
                    Log(std::format("fetch of {}: cancelled for a sync", ToUtf8(wanted.native())));
                    known.reset();
                }
                else
                {
                    LogException(std::format("fetch of {} (the sync button shows the server's state as unknown)", ToUtf8(wanted.native())));
                    known = false;
                }
            }
            catch (...)
            {
                LogException(std::format("fetch of {} (the sync button shows the server's state as unknown)", ToUtf8(wanted.native())));
                known = false;
            }
            UiQueue::Post("fetch finished", [this, mine, current, known]
            {
                auto latest = current.lock();
                if (!latest || *latest != mine) return; // another folder, or this object is gone
                fetching = false;
                if (known) remoteKnown = *known;
                if (fetched) fetched();
            });
        });
    }
}
