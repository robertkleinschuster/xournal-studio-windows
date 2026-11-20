#include "RemoteFetch.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/git/GitCredentials.h"
#include "xournal/git/GitSync.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr auto MinimumInterval = std::chrono::minutes(5);
    }

    RemoteFetch::~RemoteFetch()
    {
        ++*generation; // a fetch still running is not reported
    }

    void RemoteFetch::SetFolder(std::optional<std::filesystem::path> next)
    {
        folder = std::move(next);
        ++*generation;
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
        std::uint64_t mine = *generation;
        std::weak_ptr<std::uint64_t> current = generation;
        UiQueue::RunInBackground("fetch", [this, mine, current, wanted = *folder]
        {
            bool known = true;
            try
            {
                if (auto root = git::GitSync::FindRoot(wanted))
                {
                    if (git::GitSync::Fetch(*root, git::GitCredentials::FromCredentialManager()))
                    {
                        Log(std::format("fetch: {}", ToUtf8(root->native())));
                    }
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
                remoteKnown = known;
                if (fetched) fetched();
            });
        });
    }
}
