#pragma once

// File operations of the explorer: new folder, rename, delete to the recycle bin, and the path arithmetic around them.
// Each acts on exactly the item it is given - no companion files are renamed or deleted along with it. Failures throw
// xournal::Error (HRESULT + system text).

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::project::ProjectFiles
{
    using std::filesystem::path;

    inline constexpr wchar_t DocumentExtension[] = L".xopp";

    /// Why `name` cannot be a file or folder name (German, for the dialog), or nothing.
    std::optional<std::wstring> ValidateName(std::wstring_view name);

    /// "Name", or "Name (2)", "Name (3)", ... like File Explorer, so that nothing in the folder is overwritten.
    std::wstring UniqueName(path const& folder, std::wstring_view name, std::wstring_view extension = {});

    /// True when `p` is `item` or lies in the folder `item` (full paths, case ignored); false for nothing.
    bool IsAtOrBelow(std::optional<path> const& p, path const& item);

    /// Where `p` is after `item` was renamed to `renamed` (unchanged when it is not at or below `item`).
    path MapRenamed(path const& p, path const& item, path const& renamed);

    /// The name an item gets: a file keeps its extension (only the name before it is edited, as the dialog shows it).
    std::wstring NewName(path const& p, bool isFolder, std::wstring_view editedName);

    /// The part of the name the rename dialog shows: a file without its extension.
    std::wstring EditableName(path const& p, bool isFolder);

    /// The same file or folder (full paths, case ignored like Windows); false if one is nothing.
    bool SamePath(std::optional<path> const& a, std::optional<path> const& b);

    /// Renames a file or folder in its folder (MoveFileEx, never over an existing item). Returns the new path.
    path Rename(path const& item, std::wstring_view newName);

    /// Creates a folder; fails if the name is taken. Returns its path.
    path CreateFolder(path const& parent, std::wstring_view name);

    /// Moves a file or folder to the recycle bin (IFileOperation with FOFX_RECYCLEONDELETE). Windows asks (owned by
    /// `owner`) before an item that does not fit the recycle bin would be deleted for good; declined, nothing is deleted
    /// and this throws (E_ABORT). Call on a thread with a single-threaded apartment (IFileOperation requires it).
    void Recycle(path const& item, HWND owner = nullptr);
    /// The same for several items in one operation (nothing to do for none).
    void Recycle(std::vector<path> const& items, HWND owner = nullptr);

    /// Puts a copy of each file into the recycle bin, the files themselves stay: copied under the same name into a
    /// temporary folder, recycled from there (one operation), the folder removed. Before content is overwritten (a
    /// discard), so the current version can always be taken back from the bin. Throws when any copy fails - then
    /// nothing was recycled.
    void RecycleCopies(std::vector<path> const& files, HWND owner = nullptr);

    /// Where one item of a Transfer went.
    struct Transferred
    {
        path from;
        path to;
    };

    /// Copies or moves files and folders into `folder` with the shell (IFileOperation), as File Explorer's paste does:
    /// Windows' own progress and name-conflict questions (owned by `owner`), undoable in File Explorer; a copy into the
    /// item's own folder gets a new name ("Name - Kopie"); moving an item into its own folder does nothing. Returns where
    /// each item went (the user may have skipped some or chosen other names). Throws on failure; cancelled by the
    /// user, it returns what was done until then. Single-threaded apartment required.
    std::vector<Transferred> Transfer(std::vector<path> const& items, path const& folder, bool move, HWND owner = nullptr);
}
