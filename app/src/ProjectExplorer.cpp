#include "ProjectExplorer.h"

#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <format>

namespace xournal::app
{
    ProjectExplorer::ProjectExplorer(ExplorerTree& explorerTree)
        : tree(explorerTree), delayed("explorer refresh", std::chrono::milliseconds(400), [this] { ReadInBackground(); })
    {
    }

    ProjectExplorer::~ProjectExplorer()
    {
        ++*generation; // a read still running is not shown
        watcher.reset();
    }

    void ProjectExplorer::SetFolder(std::optional<std::filesystem::path> next)
    {
        folder = std::move(next);
        watcher.reset();
        if (folder)
        {
            try
            {
                // Runs on the watcher's thread: only restarts the delay (thread-safe).
                watcher = std::make_unique<project::FolderWatcher>(*folder, [this] { delayed.Restart(); });
            }
            catch (...)
            {
                LogException(std::format("explorer: watching {} (refreshed on activation only)", ToUtf8(folder->native())));
            }
        }
        RefreshNow();
    }

    void ProjectExplorer::Refresh()
    {
        delayed.Restart();
    }

    void ProjectExplorer::RefreshNow()
    {
        delayed.Cancel();
        ++*generation;
        ProjectSnapshot snapshot = ProjectSnapshot::Read(folder);
        syncStatus = snapshot.sync;
        tree.Show(std::move(snapshot));
        if (refreshed) refreshed();
    }

    void ProjectExplorer::ReadInBackground()
    {
        std::uint64_t mine = ++*generation;
        std::weak_ptr<std::uint64_t> current = generation;
        UiQueue::RunInBackground("explorer: read project folder", [this, mine, current, wanted = folder]
        {
            auto snapshot = std::make_shared<ProjectSnapshot>(ProjectSnapshot::Read(wanted));
            UiQueue::Post("explorer: show project folder", [this, mine, current, snapshot]
            {
                auto latest = current.lock();
                if (latest && *latest == mine) // still wanted, and this object still exists
                {
                    syncStatus = snapshot->sync;
                    tree.Show(std::move(*snapshot));
                    if (refreshed) refreshed();
                }
            });
        });
    }
}
