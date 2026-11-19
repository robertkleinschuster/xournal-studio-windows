#pragma once

// A todo.txt file, changed line by line: each change reads the file again right before (so what other apps or a git
// pull wrote meanwhile stays), touches only its own line and keeps the file's line endings, encoding and final newline.
// Written to "<file>~" first, then replaced (io::Files::WriteReplacing). The lines' meaning (TodoTxt) is the model's.
//
// Encodings: UTF-8 (todo.txt's encoding; with or without byte order mark) or UTF-16 with byte order mark, written back
// the same way. Anything else (an ANSI file, UTF-32) is refused rather than damaged: xournal::Error with
// HRESULT_FROM_WIN32(ERROR_INVALID_DATA), the file unchanged.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::tasks
{
    class TaskFile
    {
    public:
        explicit TaskFile(std::filesystem::path file) : file(std::move(file)) {}

        std::filesystem::path const& Path() const { return file; }

        bool Exists() const;

        /// The lines without line endings (none if the file does not exist).
        std::vector<std::wstring> ReadLines() const;

        /// Replaces the first line equal to `oldLine`. False: it is no longer there (nothing written).
        bool Replace(std::wstring_view oldLine, std::wstring_view newLine);

        /// Removes the first line equal to `line`. False: it is no longer there (nothing written).
        bool Remove(std::wstring_view line);

        /// Adds a line at the end (creates the file if needed; its folder must exist).
        void Append(std::wstring_view line);

    private:
        std::filesystem::path file;
    };
}
