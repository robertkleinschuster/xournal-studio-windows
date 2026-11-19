#include "TextMatch.h"

#include "xournal/base/Error.h"

#include <windows.h>

#include <algorithm>

namespace xournal::search::detail
{
    namespace
    {
        bool IsBlank(wchar_t c)
        {
            return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0 || c == 0xFFFE || c == 0x0002;
        }

        // The text with whitespace runs as one space (and no leading/trailing space), and where each of its characters
        // starts in the original (one extra entry: the original's end).
        struct Normalized
        {
            std::wstring text;
            std::vector<size_t> origin;
        };

        Normalized Normalize(std::wstring_view s)
        {
            Normalized n;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (IsBlank(s[i]))
                {
                    if (!n.text.empty() && n.text.back() != L' ')
                    {
                        n.text += L' ';
                        n.origin.push_back(i);
                    }
                    continue;
                }
                n.text += s[i];
                n.origin.push_back(i);
            }
            while (!n.text.empty() && n.text.back() == L' ')
            {
                n.text.pop_back();
                n.origin.pop_back();
            }
            n.origin.push_back(s.size());
            return n;
        }
    }

    std::vector<Match> FindMatches(std::wstring_view text, std::wstring_view query, bool matchCase, bool wholeWord)
    {
        std::vector<Match> matches;
        auto source = Normalize(text);
        auto needle = Normalize(query).text;
        if (needle.empty() || source.text.empty()) return matches;
        DWORD flags = FIND_FROMSTART | (matchCase ? 0 : LINGUISTIC_IGNORECASE);
        size_t from = 0;
        while (from < source.text.size())
        {
            int length = 0;
            SetLastError(ERROR_SUCCESS); // "not found" also returns -1: only a set error is one
            int at = FindNLSStringEx(L"de-DE", flags, source.text.data() + from, static_cast<int>(source.text.size() - from),
                                     needle.data(), static_cast<int>(needle.size()), &length, nullptr, nullptr, 0);
            if (at < 0)
            {
                DWORD error = GetLastError();
                if (error != ERROR_SUCCESS) ThrowHr("search: FindNLSStringEx", HRESULT_FROM_WIN32(error));
                break; // not found
            }
            size_t begin = from + static_cast<size_t>(at), end = begin + static_cast<size_t>(std::max(length, 1));
            bool whole = !wholeWord || ((begin == 0 || !IsCharAlphaNumericW(source.text[begin - 1])) &&
                                        (end >= source.text.size() || !IsCharAlphaNumericW(source.text[end])));
            if (whole)
            {
                // The last character of the match ends where the original's next character starts (or the text ends).
                size_t originalEnd = source.origin[end - 1] + 1;
                matches.push_back({ source.origin[begin], originalEnd });
            }
            from = begin + 1;
        }
        return matches;
    }

    std::wstring ContextOf(std::wstring_view text, Match const& match, size_t around)
    {
        size_t from = match.begin > around ? match.begin - around : 0;
        size_t to = std::min(text.size(), match.end + around);
        return Normalize(text.substr(from, to - from)).text;
    }
}
