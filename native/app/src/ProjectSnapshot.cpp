#include "ProjectSnapshot.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <format>

namespace xournal::app
{
    git::FileState ProjectSnapshot::StateOf(std::filesystem::path const& p) const
    {
        auto it = status.find(p.native());
        return it == status.end() ? git::FileState::Unchanged : it->second;
    }

    ProjectSnapshot ProjectSnapshot::Read(std::optional<std::filesystem::path> const& folder)
    {
        ProjectSnapshot snapshot;
        snapshot.folder = folder;
        if (!folder)
        {
            return snapshot;
        }
        snapshot.root = project::ProjectTree::Read(*folder);
        try
        {
            snapshot.gitRoot = git::GitSync::FindRoot(*folder);
            if (snapshot.gitRoot)
            {
                snapshot.status = git::GitSync::WithFolders(git::GitSync::GetStatus(*snapshot.gitRoot), *snapshot.gitRoot);
            }
        }
        catch (...)
        {
            LogException(std::format("explorer: git status of {} (shown without colors)", ToUtf8(folder->native())));
            snapshot.status.clear();
        }
        return snapshot;
    }
}
