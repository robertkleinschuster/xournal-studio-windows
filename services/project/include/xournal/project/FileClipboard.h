#pragma once

// Files on the Windows clipboard as File Explorer puts them there and reads them: CF_HDROP (the list of full paths)
// with "Preferred DropEffect" (move for Ausschneiden, copy for Kopieren), so cut/copy/paste works between the explorer
// and File Explorer both ways. Failures throw xournal::Error.

#include <windows.h>
#include <shellapi.h> // HDROP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace xournal::project::FileClipboard
{
    using std::filesystem::path;

    struct Content
    {
        std::vector<path> files;
        bool move = false; // cut: the paste moves them
    };

    /// The CF_HDROP block for `files`: a DROPFILES header followed by the wide paths, each ended by a 0, and one more 0.
    std::vector<std::uint8_t> DropBlock(std::vector<path> const& files);

    /// The paths of a CF_HDROP handle (DragQueryFileW).
    std::vector<path> FilesOf(HDROP drop);

    /// Puts the files on the clipboard (replacing what was there); `move`: cut.
    void Put(HWND owner, std::vector<path> const& files, bool move);

    /// The files on the clipboard, or nothing when it holds none.
    std::optional<Content> Get(HWND owner);

    /// Empties the clipboard (after a cut was pasted: the files are no longer where it says).
    void Clear(HWND owner);
}
