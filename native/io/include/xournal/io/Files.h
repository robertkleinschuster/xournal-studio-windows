#pragma once

// Whole-file reads and the one way the app replaces a file: write "<file>~" completely, then move it over the file
// (MoveFileEx), so a failure never leaves a half-written document.

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace xournal::io::Files
{
    std::vector<std::uint8_t> ReadAll(std::filesystem::path const& file);

    /// Writes "<file>~", then replaces `file` with it. On failure the "~" file of this write is removed again (a failure
    /// doing so is logged and named in the thrown error) and `file` is untouched.
    void WriteReplacing(std::filesystem::path const& file, std::span<std::uint8_t const> data);

    /// Copies a file to a new name; never over an existing file (that throws).
    void CopyNew(std::filesystem::path const& from, std::filesystem::path const& to);

    /// Deletes a file this app just made (taking back a failed write).
    void Delete(std::filesystem::path const& file);
}
