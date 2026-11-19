#pragma once

// Text helpers the modules share: UTF-8 <-> UTF-16 (the XML tree and Windows use UTF-16, messages and logs UTF-8) and
// the ordinal, case-insensitive comparison Windows uses for file names.

#include <string>
#include <string_view>
#include <vector>

namespace xournal
{
    std::string ToUtf8(std::wstring_view text);
    std::wstring FromUtf8(std::string_view text);

    /// Equal as Windows compares file names (ordinal, ignoring case: CompareStringOrdinal).
    bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b);
    bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix);

    /// Ordinal, case-insensitive order (CompareStringOrdinal): <0, 0, >0.
    int CompareIgnoreCase(std::wstring_view a, std::wstring_view b);

    /// For maps keyed by file paths or names as Windows compares them.
    struct LessIgnoreCase
    {
        using is_transparent = void;
        bool operator()(std::wstring_view a, std::wstring_view b) const { return CompareIgnoreCase(a, b) < 0; }
    };

    bool IsSpace(wchar_t c);
    std::wstring_view Trim(std::wstring_view text);

    /// The parts between runs of whitespace (no empty parts).
    std::vector<std::wstring_view> SplitWhitespace(std::wstring_view text);

    /// The parts between `separator`s, empty parts left out.
    std::vector<std::wstring_view> Split(std::wstring_view text, wchar_t separator);
}
