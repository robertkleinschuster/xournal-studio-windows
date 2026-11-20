#pragma once

// The tasks list as the Aufgaben pane shows it: the todo.txt lines parsed (model's TodoTxt), open tasks by due date and
// time (none last), then - if wanted - the done ones, newest first; each with its due text ("heute 10:00", "morgen",
// "Mo 24.11."; whether it is overdue is a flag) and its linked page (doc: tag).

#include "xournal/model/TodoTxt.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xournal::app
{
    struct TaskRow
    {
        std::wstring line;      // as in the file (changes look it up by this)
        TodoTask task;
        std::wstring title;     // without key:value tags
        std::wstring dueText;   // empty: no due date
        bool overdue = false;
        bool dueSoon = false;   // today or tomorrow
        std::optional<std::pair<std::filesystem::path, int>> link; // document and 0-based page (-1: none)
        std::wstring linkText;  // "Blatt 3.xopp, Seite 2"
    };

    struct TaskRows
    {
        std::vector<TaskRow> rows;
        int openCount = 0;

        static TaskRows From(std::vector<std::wstring> const& lines, std::filesystem::path const& taskFile, bool withDone);
    };

    /// Today in local time.
    Date Today();
}
