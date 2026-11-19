#pragma once

// File paths as Windows treats them: full paths by GetFullPathName, names compared ignoring case. The document's
// references (PDF, pictures) are made relative with these, as .NET's Path did for the C# app.

#include <filesystem>
#include <string>

namespace xournal::Paths
{
    using std::filesystem::path;

    /// The full, normalized path (GetFullPathName: '/' become '\', "." and ".." are resolved).
    path Full(path const& p);

    /// The same file as Windows names it (full paths, ignoring case).
    bool Same(path const& a, path const& b);

    /// The folder of a file's full path.
    path DirectoryOf(path const& file);

    /// `target` relative to the folder `base` (both made full, compared ignoring case); the full target when they are on
    /// different drives; "." for the folder itself.
    path Relative(path const& base, path const& target);

    /// Rooted: a drive or a leading separator ("Z:/x", "\x"), not relative to a folder.
    bool IsRooted(std::wstring const& p);

    /// The path with '/' separators (as the file stores references, portable between machines).
    std::wstring WithForwardSlashes(path const& p);

    /// Whether a file (not a folder) exists; errors other than "not found" are thrown.
    bool FileExists(path const& p);
}
