#pragma once

// libgit2 for this module: owning pointers for its objects, the library's init/shutdown scope and its errors as
// xournal::Error. Internal. (Not named Git2.h: on a case-insensitive file system that would be found for <git2.h>.)

#include <git2.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace xournal::git::git2
{
    template <auto Free>
    struct Deleter
    {
        template <typename T>
        void operator()(T* p) const { Free(p); }
    };

    using Repository = std::unique_ptr<git_repository, Deleter<git_repository_free>>;
    using Reference = std::unique_ptr<git_reference, Deleter<git_reference_free>>;
    using Commit = std::unique_ptr<git_commit, Deleter<git_commit_free>>;
    using Tree = std::unique_ptr<git_tree, Deleter<git_tree_free>>;
    using Object = std::unique_ptr<git_object, Deleter<git_object_free>>;
    using Index = std::unique_ptr<git_index, Deleter<git_index_free>>;
    using Remote = std::unique_ptr<git_remote, Deleter<git_remote_free>>;
    using Signature = std::unique_ptr<git_signature, Deleter<git_signature_free>>;
    using StatusList = std::unique_ptr<git_status_list, Deleter<git_status_list_free>>;
    using Diff = std::unique_ptr<git_diff, Deleter<git_diff_free>>;
    using Config = std::unique_ptr<git_config, Deleter<git_config_free>>;
    using AnnotatedCommit = std::unique_ptr<git_annotated_commit, Deleter<git_annotated_commit_free>>;
    using Rebase = std::unique_ptr<git_rebase, Deleter<git_rebase_free>>;
    using ConflictIterator = std::unique_ptr<git_index_conflict_iterator, Deleter<git_index_conflict_iterator_free>>;

    struct Buffer
    {
        git_buf buf = GIT_BUF_INIT;
        Buffer() = default;
        Buffer(Buffer const&) = delete;
        Buffer& operator=(Buffer const&) = delete;
        ~Buffer() { git_buf_dispose(&buf); }
        std::string Text() const { return buf.ptr ? std::string(buf.ptr, buf.size) : std::string(); }
    };

    /// git_libgit2_init for the scope (reference counted by libgit2); a failure is thrown.
    class Library
    {
    public:
        Library();
        ~Library();
        Library(Library const&) = delete;
        Library& operator=(Library const&) = delete;
    };

    /// Logs and throws "<step>: <libgit2's message> (<code>)" for a negative result; returns the result otherwise.
    int Check(std::string_view step, int result);

    Repository Open(std::wstring const& path);

    /// The working directory (with a separator at its end, as libgit2 gives it); a bare repository is thrown.
    std::wstring Workdir(git_repository* repo);

    /// The full Windows path of a path relative to the working directory, as git names it ('/' separators).
    std::filesystem::path FullPath(git_repository* repo, char const* relative);
}
