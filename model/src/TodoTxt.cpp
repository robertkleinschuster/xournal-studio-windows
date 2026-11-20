#include "xournal/model/TodoTxt.h"

#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <windows.h>

#include <format>
#include <vector>

namespace xournal
{
    namespace
    {
        struct Token
        {
            size_t start = 0;
            std::wstring_view text;
        };

        std::vector<Token> Tokens(std::wstring_view text)
        {
            std::vector<Token> tokens;
            size_t i = 0;
            while (i < text.size())
            {
                while (i < text.size() && IsSpace(text[i])) ++i;
                size_t start = i;
                while (i < text.size() && !IsSpace(text[i])) ++i;
                if (i > start)
                {
                    tokens.push_back({ start, text.substr(start, i - start) });
                }
            }
            return tokens;
        }

        bool IsLetter(wchar_t c)
        {
            WORD type = 0;
            return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_ALPHA) != 0;
        }

        struct TagParts
        {
            std::wstring_view key, value;
        };

        // A key:value tag: the key starts with a letter and holds no ':'; the value is the rest of the token.
        std::optional<TagParts> AsTag(std::wstring_view token)
        {
            size_t colon = token.find(L':');
            if (colon == std::wstring_view::npos || colon == 0 || colon + 1 >= token.size() || !IsLetter(token[0]))
            {
                return std::nullopt;
            }
            return TagParts{ token.substr(0, colon), token.substr(colon + 1) };
        }

        bool IsUrl(TagParts const& tag) { return tag.value.starts_with(L"//"); }

        // Runs of two or more whitespace characters become one space, then the ends are trimmed (as the C# app's
        // Regex.Replace(@"\s{2,}", " ").Trim()).
        std::wstring Collapse(std::wstring_view text)
        {
            std::wstring result;
            size_t i = 0;
            while (i < text.size())
            {
                if (!IsSpace(text[i]))
                {
                    result += text[i++];
                    continue;
                }
                size_t start = i;
                while (i < text.size() && IsSpace(text[i])) ++i;
                result += i - start >= 2 ? std::wstring(L" ") : std::wstring(1, text[start]);
            }
            return std::wstring(Trim(result));
        }

        bool AllDigits(std::wstring_view s)
        {
            if (s.empty()) return false;
            for (wchar_t c : s)
            {
                if (c < L'0' || c > L'9') return false;
            }
            return true;
        }

        int ToInt(std::wstring_view digits)
        {
            int value = 0;
            for (wchar_t c : digits) value = value * 10 + (c - L'0');
            return value;
        }

        bool IsValidDate(int y, int m, int d)
        {
            static constexpr int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
            if (y < 1 || m < 1 || m > 12 || d < 1) return false;
            bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
            return d <= days[m - 1] + (m == 2 && leap ? 1 : 0);
        }

        std::optional<Date> TakeDate(std::wstring_view& rest)
        {
            if (rest.size() >= 10)
            {
                auto date = TodoTxt::ParseDate(rest.substr(0, 10));
                if (date && (rest.size() == 10 || rest[10] == L' '))
                {
                    rest = Trim(rest.substr(10)); // only leading space can be left: the line was trimmed
                    return date;
                }
            }
            return std::nullopt;
        }

        int HexValue(wchar_t c)
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        }

        bool IsEscape(std::wstring_view text, size_t i)
        {
            return i + 2 < text.size() && text[i] == L'%' && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0;
        }

        // Percent-decoding as Uri.UnescapeDataString: %XX bytes are UTF-8; a run that is not valid UTF-8 stays as written.
        std::wstring Unescape(std::wstring_view text)
        {
            std::wstring result;
            size_t i = 0;
            while (i < text.size())
            {
                if (!IsEscape(text, i))
                {
                    result += text[i++];
                    continue;
                }
                size_t start = i;
                std::string bytes;
                for (; IsEscape(text, i); i += 3)
                {
                    bytes.push_back(static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2])));
                }
                int size = static_cast<int>(bytes.size());
                int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
                if (length <= 0)
                {
                    result += text.substr(start, i - start); // not UTF-8: kept as written, as .NET does
                    continue;
                }
                std::wstring decoded(static_cast<size_t>(length), L'\0');
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), size, decoded.data(), length);
                result += decoded;
            }
            return result;
        }
    }

    std::optional<std::wstring> TodoTask::Tag(std::wstring_view key) const
    {
        for (auto const& token : Tokens(description))
        {
            auto tag = AsTag(token.text);
            if (tag && tag->key == key && !IsUrl(*tag))
            {
                return std::wstring(tag->value);
            }
        }
        return std::nullopt;
    }

    std::optional<Date> TodoTask::Due() const
    {
        auto due = Tag(L"due");
        return due ? TodoTxt::ParseDate(*due) : std::nullopt;
    }

    std::optional<TimeOfDay> TodoTask::Time() const
    {
        auto time = Tag(L"time");
        if (!time)
        {
            return std::nullopt;
        }
        size_t colon = time->find(L':');
        if (colon == std::wstring::npos)
        {
            return std::nullopt;
        }
        std::wstring_view hours = std::wstring_view(*time).substr(0, colon), minutes = std::wstring_view(*time).substr(colon + 1);
        if (!AllDigits(hours) || hours.size() > 2 || !AllDigits(minutes) || minutes.size() != 2)
        {
            return std::nullopt;
        }
        int h = ToInt(hours), m = ToInt(minutes);
        if (h > 23 || m > 59)
        {
            return std::nullopt;
        }
        return TimeOfDay{ h, m };
    }

    std::wstring TodoTask::Title() const
    {
        std::wstring kept;
        size_t from = 0;
        for (auto const& token : Tokens(description))
        {
            auto tag = AsTag(token.text);
            if (tag && !IsUrl(*tag))
            {
                kept += description.substr(from, token.start - from);
                from = token.start + token.text.size();
            }
        }
        kept += description.substr(from);
        return Collapse(kept);
    }

    TodoTask TodoTask::Complete(Date today) const
    {
        if (done)
        {
            return *this;
        }
        TodoTask task = *this;
        task.done = true;
        task.completed = today;
        task.priority.reset();
        if (priority)
        {
            task.description = TodoTxt::SetTag(description, L"pri", std::wstring(1, *priority));
        }
        return task;
    }

    TodoTask TodoTask::Reopen() const
    {
        if (!done)
        {
            return *this;
        }
        auto pri = Tag(L"pri");
        bool hasPriority = pri && pri->size() == 1 && (*pri)[0] >= L'A' && (*pri)[0] <= L'Z';
        TodoTask task = *this;
        task.done = false;
        task.completed.reset();
        if (hasPriority)
        {
            task.priority = (*pri)[0];
            task.description = TodoTxt::SetTag(description, L"pri", std::nullopt);
        }
        return task;
    }

    std::wstring TodoTask::ToString() const { return TodoTxt::Format(*this); }

    namespace TodoTxt
    {
        std::optional<TodoTask> Parse(std::wstring_view line)
        {
            std::wstring_view rest = Trim(line);
            if (rest.empty())
            {
                return std::nullopt;
            }
            TodoTask task;
            if (rest.starts_with(L"x "))
            {
                task.done = true;
                rest = Trim(rest.substr(2));
                if (auto completed = TakeDate(rest))
                {
                    task.completed = completed;
                    task.created = TakeDate(rest);
                }
            }
            else
            {
                if (rest.size() >= 4 && rest[0] == L'(' && rest[1] >= L'A' && rest[1] <= L'Z' && rest[2] == L')' && rest[3] == L' ')
                {
                    task.priority = rest[1];
                    rest = Trim(rest.substr(4));
                }
                task.created = TakeDate(rest);
            }
            task.description = rest;
            return task;
        }

        std::wstring Format(TodoTask const& task)
        {
            std::wstring line;
            if (task.done)
            {
                line += L"x ";
                if (task.completed)
                {
                    line += FormatDate(*task.completed) + L" ";
                }
            }
            else if (task.priority)
            {
                line += std::format(L"({}) ", *task.priority);
            }
            if (task.created && (!task.done || task.completed))
            {
                line += FormatDate(*task.created) + L" ";
            }
            return line + task.description;
        }

        TodoTask Create(std::wstring_view text, Date today, std::optional<Date> due)
        {
            TodoTask task;
            task.created = today;
            task.description = Trim(text);
            if (due)
            {
                task.description = SetTag(task.description, L"due", FormatDate(*due));
            }
            return task;
        }

        std::wstring EditText(TodoTask const& task)
        {
            std::wstring text = SetTag(SetTag(task.description, L"due", std::nullopt), L"doc", std::nullopt);
            return !task.done && task.priority ? std::format(L"({}) {}", *task.priority, text) : text;
        }

        TodoTask WithEditText(TodoTask const& task, std::wstring_view text)
        {
            TodoTask changed = task;
            std::wstring_view rest = Trim(text);
            if (!task.done)
            {
                changed.priority.reset();
                if (rest.size() >= 4 && rest[0] == L'(' && rest[1] >= L'A' && rest[1] <= L'Z' && rest[2] == L')' && rest[3] == L' ')
                {
                    changed.priority = rest[1];
                    rest = Trim(rest.substr(4));
                }
            }
            changed.description = rest;
            if (auto due = task.Tag(L"due")) changed.description = SetTag(changed.description, L"due", *due);
            if (auto doc = task.Tag(L"doc")) changed.description = SetTag(changed.description, L"doc", *doc);
            return changed;
        }

        std::wstring SetTag(std::wstring_view description, std::wstring_view key, std::optional<std::wstring> const& value)
        {
            std::wstring prefix = std::wstring(key) + L":";
            for (auto const& token : Tokens(description))
            {
                if (token.text.size() > prefix.size() && token.text.starts_with(prefix))
                {
                    std::wstring replaced(description.substr(0, token.start));
                    if (value)
                    {
                        replaced += prefix + *value;
                    }
                    replaced += description.substr(token.start + token.text.size());
                    return Collapse(replaced);
                }
            }
            if (!value)
            {
                return std::wstring(description);
            }
            return std::wstring(Trim(std::wstring(description) + L" " + prefix + *value));
        }

        std::optional<Date> ParseDate(std::wstring_view text)
        {
            if (text.size() != 10 || text[4] != L'-' || text[7] != L'-' || !AllDigits(text.substr(0, 4)) ||
                !AllDigits(text.substr(5, 2)) || !AllDigits(text.substr(8, 2)))
            {
                return std::nullopt;
            }
            Date date{ ToInt(text.substr(0, 4)), ToInt(text.substr(5, 2)), ToInt(text.substr(8, 2)) };
            return IsValidDate(date.year, date.month, date.day) ? std::optional<Date>(date) : std::nullopt;
        }

        std::wstring FormatDate(Date d) { return std::format(L"{:04}-{:02}-{:02}", d.year, d.month, d.day); }

        std::wstring DocumentLink(std::filesystem::path const& taskFile, std::filesystem::path const& document, int pageIndex)
        {
            std::wstring relative = Paths::WithForwardSlashes(Paths::Relative(Paths::DirectoryOf(taskFile), document));
            std::wstring link;
            for (wchar_t c : relative)
            {
                switch (c)
                {
                case L' ': link += L"%20"; break;
                case L'%': link += L"%25"; break;
                case L'#': link += L"%23"; break;
                default: link += c; break;
                }
            }
            return pageIndex >= 0 ? std::format(L"{}#p{}", link, pageIndex + 1) : link;
        }

        std::optional<std::pair<std::filesystem::path, int>> ResolveDocumentLink(std::filesystem::path const& taskFile,
            std::wstring_view value)
        {
            int page = -1;
            size_t hash = value.rfind(L"#p");
            if (hash != std::wstring_view::npos)
            {
                std::wstring_view digits = value.substr(hash + 2);
                if (!AllDigits(digits) || digits.size() > 9 || ToInt(digits) < 1)
                {
                    return std::nullopt;
                }
                page = ToInt(digits) - 1;
                value = value.substr(0, hash);
            }
            std::wstring path = Unescape(value);
            if (path.empty())
            {
                return std::nullopt;
            }
            for (auto& c : path)
            {
                if (c == L'/') c = L'\\';
            }
            return std::pair{ Paths::Full(Paths::DirectoryOf(taskFile) / path), page };
        }
    }
}
