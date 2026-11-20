#include "LibGit2.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <format>

namespace xournal::git::git2
{
    Library::Library()
    {
        Check("git_libgit2_init", git_libgit2_init());
    }

    Library::~Library()
    {
        if (int result = git_libgit2_shutdown(); result < 0)
        {
            LogError(std::format("git_libgit2_shutdown: {}", result));
        }
    }

    int Check(std::string_view step, int result)
    {
        if (result >= 0)
        {
            return result;
        }
        git_error const* error = git_error_last();
        std::string message = std::format("git {}: {} ({})", step,
            error && error->message ? error->message : "no message from libgit2", result);
        LogError(message);
        throw Error(message, E_FAIL);
    }

    std::wstring Workdir(git_repository* repo)
    {
        char const* workdir = git_repository_workdir(repo);
        if (!workdir)
        {
            std::string message = "git: the repository has no working directory (bare)";
            LogError(message);
            throw Error(message, E_FAIL);
        }
        return FromUtf8(workdir);
    }

    std::filesystem::path FullPath(git_repository* repo, char const* relative)
    {
        return Paths::Full(Workdir(repo) + FromUtf8(relative));
    }

    Repository Open(std::wstring const& path)
    {
        git_repository* repo = nullptr;
        Check(std::format("open repository {}", ToUtf8(path)), git_repository_open(&repo, ToUtf8(path).c_str()));
        return Repository(repo);
    }
}
