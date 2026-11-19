#pragma once

// Internal: finding a query in a text the German way - FindNLSStringEx in de-DE (linguistic: ä = Ä and ß = SS when
// case is ignored), whitespace runs (also line breaks and PDFium's generated characters) as one space, whole words
// by the characters around a match (IsCharAlphaNumericW).

#include <string>
#include <string_view>
#include <vector>

namespace xournal::search::detail
{
    struct Match
    {
        size_t begin = 0, end = 0; // in the original text, end exclusive
    };

    std::vector<Match> FindMatches(std::wstring_view text, std::wstring_view query, bool matchCase, bool wholeWord);

    /// The match with up to `around` characters on each side, whitespace runs as one space.
    std::wstring ContextOf(std::wstring_view text, Match const& match, size_t around = 40);
}
