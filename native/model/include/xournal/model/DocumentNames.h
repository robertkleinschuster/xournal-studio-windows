#pragma once

// Document names: upstream Xournal++'s default "%F-Notiz-%H-%M" (the German translation of "%F-Note-%H-%M"), or the
// name of the document's file or PDF.

#include <filesystem>
#include <optional>
#include <string>

namespace xournal
{
    /// Local date and time to the minute.
    struct DateTime
    {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0;

        static DateTime Now(); // GetLocalTime
        bool operator==(DateTime const&) const = default;
    };

    namespace DocumentNames
    {
        /// "2026-11-19-Notiz-14-05".
        std::wstring DefaultName(DateTime created);

        /// The document's name without extension: its file's, else its PDF's, else the default name.
        std::wstring NameFor(std::optional<std::filesystem::path> const& file, std::optional<std::filesystem::path> const& pdf,
            DateTime created);
    }
}
