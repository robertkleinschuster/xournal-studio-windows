// "Änderungen verwerfen" of the explorer: which files of a file or folder go back to the last commit and which are new,
// and the git part of it (checkout from HEAD, new files out of the index).

#include "xournal/git/GitSync.h"

#include "LibGit2.h"

#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"

#include <windows.h>

#include <algorithm>
#include <format>

namespace xournal::git
{
    using namespace git2;

    namespace
    {
        std::string GitPath(std::wstring const& workdir, path const& file)
        {
            return ToUtf8(Paths::WithForwardSlashes(Paths::Relative(workdir, file)));
        }

        void AddOnce(std::vector<path>& list, path const& file)
        {
            if (std::ranges::none_of(list, [&](path const& p) { return Paths::Same(p, file); }))
            {
                list.push_back(file);
            }
        }
    }

    DiscardPlan GitSync::PlanDiscard(path const& root, path const& item)
    {
        Library library;
        Repository repo = Open(root.native());
        // Renames in the index as GetStatus shows them: one entry with the old and the new path.
        git_status_options options = GIT_STATUS_OPTIONS_INIT;
        options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
        options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;
        git_status_list* raw = nullptr;
        Check("status", git_status_list_new(&raw, repo.get(), &options));
        StatusList list(raw);
        DiscardPlan plan;
        for (size_t i = 0, n = git_status_list_entrycount(list.get()); i < n; i++)
        {
            git_status_entry const* entry = git_status_byindex(list.get(), i);
            unsigned int s = entry->status;
            git_diff_delta const* delta = entry->head_to_index ? entry->head_to_index : entry->index_to_workdir;
            if (!delta || s == GIT_STATUS_CURRENT || (s & GIT_STATUS_IGNORED))
            {
                continue;
            }
            path file = FullPath(repo.get(), delta->new_file.path);
            bool onDisk = (s & GIT_STATUS_WT_DELETED) == 0;
            if (s & GIT_STATUS_INDEX_RENAMED)
            {
                // The old path comes back, the new one goes - also when only one of them lies in `item`.
                path old = FullPath(repo.get(), entry->head_to_index->old_file.path);
                if (!Paths::IsAtOrBelow(file, item) && !Paths::IsAtOrBelow(old, item))
                {
                    continue;
                }
                AddOnce(plan.restore, old);
                if (Paths::FileExists(old)) AddOnce(plan.overwritten, old); // e.g. a new file written at the old name
                AddOnce(onDisk ? plan.remove : plan.unstage, file);
                continue;
            }
            if (!Paths::IsAtOrBelow(file, item))
            {
                continue;
            }
            // Not in HEAD: added to the index only, or untracked (unless it is a file the index has as deleted).
            bool inHead = (s & GIT_STATUS_INDEX_NEW) == 0 && ((s & GIT_STATUS_WT_NEW) == 0 || (s & GIT_STATUS_INDEX_DELETED) != 0);
            if (inHead)
            {
                AddOnce(plan.restore, file);
                if (onDisk && Paths::FileExists(file)) AddOnce(plan.overwritten, file);
            }
            else
            {
                AddOnce(onDisk ? plan.remove : plan.unstage, file);
            }
        }
        return plan;
    }

    void GitSync::Discard(path const& root, DiscardPlan const& plan)
    {
        Library library;
        Repository repo = Open(root.native());
        std::wstring workdir = Workdir(repo.get());
        if (!plan.remove.empty() || !plan.unstage.empty())
        {
            git_index* rawIndex = nullptr;
            Check("index", git_repository_index(&rawIndex, repo.get()));
            Index index(rawIndex);
            for (auto const* files : { &plan.remove, &plan.unstage })
            {
                for (path const& file : *files)
                {
                    std::string relative = GitPath(workdir, file);
                    if (git_index_get_bypath(index.get(), relative.c_str(), 0))
                    {
                        Check(std::format("unstage {}", relative), git_index_remove_bypath(index.get(), relative.c_str()));
                    }
                }
            }
            Check("write index", git_index_write(index.get()));
        }
        if (plan.restore.empty())
        {
            return;
        }
        std::vector<std::string> relatives;
        for (path const& file : plan.restore)
        {
            relatives.push_back(GitPath(workdir, file));
        }
        std::vector<char*> paths;
        for (std::string& relative : relatives)
        {
            paths.push_back(relative.data());
        }
        git_object* head = nullptr;
        Check("HEAD commit", git_revparse_single(&head, repo.get(), "HEAD^{commit}"));
        Object owned(head);
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        checkout.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
        checkout.paths = { paths.data(), paths.size() };
        Check(std::format("discard {} file(s)", relatives.size()), git_checkout_tree(repo.get(), head, &checkout));
    }
}
