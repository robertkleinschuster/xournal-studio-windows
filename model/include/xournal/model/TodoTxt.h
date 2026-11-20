#pragma once

// One line of a todo.txt file (https://github.com/todotxt/todo.txt), e.g.
// "x 2026-11-05 2026-11-01 (A) GET HÜ Abgabe +GET due:2026-11-05 time:18:00". The description is kept verbatim (tags of
// other apps included), so a task is written back as other apps wrote it.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace xournal
{
    struct Date
    {
        int year = 0, month = 0, day = 0;
        bool operator==(Date const&) const = default;
    };

    struct TimeOfDay
    {
        int hour = 0, minute = 0;
        bool operator==(TimeOfDay const&) const = default;
    };

    struct TodoTask
    {
        bool done = false;
        std::optional<wchar_t> priority;
        std::optional<Date> completed;
        std::optional<Date> created;
        std::wstring description;

        /// The value of the first key:value tag with this key (a key starts with a letter: 10:30 is a time; a URL's
        /// "https://..." is not a tag).
        std::optional<std::wstring> Tag(std::wstring_view key) const;
        /// due:YYYY-MM-DD.
        std::optional<Date> Due() const;
        /// time:HH:MM, the due time (kept apart from due: so other apps still understand the date).
        std::optional<TimeOfDay> Time() const;
        /// The description without key:value tags (links stay), for showing.
        std::wstring Title() const;

        /// Done today. The priority goes into a pri: tag, as todo.txt apps do (a done task has none).
        TodoTask Complete(Date today) const;
        /// Open again (the priority back from pri:).
        TodoTask Reopen() const;

        std::wstring ToString() const;
    };

    namespace TodoTxt
    {
        /// Parses a line; nothing for blank lines.
        std::optional<TodoTask> Parse(std::wstring_view line);
        std::wstring Format(TodoTask const& task);
        /// A new task created today, with an optional due date.
        TodoTask Create(std::wstring_view text, Date today, std::optional<Date> due);
        /// Sets (or with nothing removes) a key:value tag; an existing one keeps its place. The value is literal.
        std::wstring SetTag(std::wstring_view description, std::wstring_view key, std::optional<std::wstring> const& value);

        /// The text an in-place edit starts with: the priority as todo.txt writes it ("(A) ", open tasks only) and the
        /// description without the due: and doc: tags (shown apart as the due date and the link).
        std::wstring EditText(TodoTask const& task);
        /// The task with an edited EditText: an open task's priority read the todo.txt way ("(B) ..." sets it, no
        /// "(X) " removes it), due: and doc: kept as they were, everything else (done, dates) unchanged. A done task's
        /// text is taken as written (todo.txt gives done tasks no priority).
        TodoTask WithEditText(TodoTask const& task, std::wstring_view text);

        std::optional<Date> ParseDate(std::wstring_view text);
        std::wstring FormatDate(Date date);

        /// The value of a doc: tag linking a document page: relative to the task file's folder with '/' (space, % and #
        /// encoded, they would end the tag or the path), the 1-based page as "#pN" - as the GTK Xournal Studio wrote.
        std::wstring DocumentLink(std::filesystem::path const& taskFile, std::filesystem::path const& document, int pageIndex);
        /// The document and 0-based page (-1: none) of a doc: value of a task in `taskFile`.
        std::optional<std::pair<std::filesystem::path, int>> ResolveDocumentLink(std::filesystem::path const& taskFile,
            std::wstring_view value);
    }
}
