#include "TaskRows.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <tuple>

namespace xournal::app
{
    namespace
    {
        SYSTEMTIME Local()
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            return now;
        }

        auto Key(Date d) { return std::tuple(d.year, d.month, d.day); }

        Date AddDays(Date d, int days)
        {
            SYSTEMTIME st{ static_cast<WORD>(d.year), static_cast<WORD>(d.month), 0, static_cast<WORD>(d.day) };
            FILETIME ft{};
            if (!SystemTimeToFileTime(&st, &ft))
            {
                ThrowLastError("SystemTimeToFileTime");
            }
            ULARGE_INTEGER value{ ft.dwLowDateTime, ft.dwHighDateTime };
            value.QuadPart += static_cast<ULONGLONG>(days) * 24 * 60 * 60 * 10'000'000;
            ft = { value.LowPart, value.HighPart };
            if (!FileTimeToSystemTime(&ft, &st))
            {
                ThrowLastError("FileTimeToSystemTime");
            }
            return { st.wYear, st.wMonth, st.wDay };
        }

        // "Mo 24.11." in German, as Windows formats it.
        std::wstring DayText(Date d)
        {
            SYSTEMTIME st{ static_cast<WORD>(d.year), static_cast<WORD>(d.month), 0, static_cast<WORD>(d.day) };
            wchar_t text[64]{};
            if (!GetDateFormatEx(L"de-AT", 0, &st, L"ddd dd.MM.", text, ARRAYSIZE(text), nullptr))
            {
                ThrowLastError("GetDateFormatEx");
            }
            return text;
        }

        void Describe(TaskRow& row, Date today)
        {
            auto due = row.task.Due();
            if (!due)
            {
                return;
            }
            auto time = row.task.Time();
            SYSTEMTIME now = Local();
            row.overdue = !row.task.done && (Key(*due) < Key(today) ||
                (*due == today && time && std::tuple(time->hour, time->minute) < std::tuple<int, int>(now.wHour, now.wMinute)));
            Date tomorrow = AddDays(today, 1);
            row.dueSoon = Key(*due) <= Key(tomorrow);
            std::wstring day = *due == today ? L"heute" : *due == tomorrow ? L"morgen" : DayText(*due);
            std::wstring at = time ? std::format(L" {:02}:{:02}", time->hour, time->minute) : L"";
            row.dueText = (row.overdue ? L"überfällig · " : L"fällig ") + day + at;
        }
    }

    Date Today()
    {
        SYSTEMTIME now = Local();
        return { now.wYear, now.wMonth, now.wDay };
    }

    TaskRows TaskRows::From(std::vector<std::wstring> const& lines, std::filesystem::path const& taskFile, bool withDone)
    {
        Date today = Today();
        std::vector<TaskRow> open, done;
        for (auto const& line : lines)
        {
            auto task = TodoTxt::Parse(line);
            if (!task)
            {
                continue;
            }
            TaskRow row{ line, *task };
            row.title = task->Title().empty() ? task->description : task->Title();
            if (auto doc = task->Tag(L"doc"))
            {
                row.link = TodoTxt::ResolveDocumentLink(taskFile, *doc);
                if (row.link)
                {
                    row.linkText = row.link->first.filename().native() +
                        (row.link->second >= 0 ? std::format(L", Seite {}", row.link->second + 1) : L"");
                }
                else
                {
                    Log(std::format("tasks: doc: link '{}' is no path (shown without link)", ToUtf8(*doc)));
                }
            }
            Describe(row, today);
            (task->done ? done : open).push_back(std::move(row));
        }
        auto dueKey = [](TaskRow const& r)
        {
            auto due = r.task.Due();
            auto time = r.task.Time();
            return std::tuple(due ? Key(*due) : std::tuple(9999, 99, 99), time ? std::tuple(time->hour, time->minute) : std::tuple(99, 99));
        };
        std::ranges::stable_sort(open, [&](TaskRow const& a, TaskRow const& b) { return dueKey(a) < dueKey(b); });
        std::ranges::stable_sort(done, [](TaskRow const& a, TaskRow const& b)
        {
            auto ka = a.task.completed ? Key(*a.task.completed) : std::tuple(0, 0, 0);
            auto kb = b.task.completed ? Key(*b.task.completed) : std::tuple(0, 0, 0);
            return ka > kb;
        });
        TaskRows result;
        result.openCount = static_cast<int>(open.size());
        result.rows = std::move(open);
        if (withDone)
        {
            std::ranges::move(done, std::back_inserter(result.rows));
        }
        return result;
    }
}
