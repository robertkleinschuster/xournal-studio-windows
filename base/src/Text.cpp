#include "xournal/base/Text.h"

#include "xournal/base/Error.h"

#include <windows.h>

#include <cwctype>

namespace xournal
{
    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
        {
            ThrowLastError("WideCharToMultiByte");
        }
        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::wstring FromUtf8(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
        {
            ThrowLastError("MultiByteToWideChar");
        }
        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        return result;
    }

    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
    {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
            CSTR_EQUAL;
    }

    int CompareIgnoreCase(std::wstring_view a, std::wstring_view b)
    {
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) -
            CSTR_EQUAL;
    }

    bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix)
    {
        return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
    }

    bool IsSpace(wchar_t c)
    {
        return std::iswspace(c) != 0;
    }

    std::wstring_view Trim(std::wstring_view text)
    {
        while (!text.empty() && IsSpace(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsSpace(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    std::vector<std::wstring_view> SplitWhitespace(std::wstring_view text)
    {
        std::vector<std::wstring_view> parts;
        size_t i = 0;
        while (i < text.size())
        {
            while (i < text.size() && IsSpace(text[i]))
            {
                ++i;
            }
            size_t start = i;
            while (i < text.size() && !IsSpace(text[i]))
            {
                ++i;
            }
            if (i > start)
            {
                parts.push_back(text.substr(start, i - start));
            }
        }
        return parts;
    }

    std::vector<std::wstring_view> Split(std::wstring_view text, wchar_t separator)
    {
        std::vector<std::wstring_view> parts;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find(separator, start);
            if (end == std::wstring_view::npos)
            {
                end = text.size();
            }
            if (end > start)
            {
                parts.push_back(text.substr(start, end - start));
            }
            start = end + 1;
        }
        return parts;
    }
}
