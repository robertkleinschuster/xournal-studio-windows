#include "xournal/model/XoppText.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"

#include <icu.h>

#include <array>
#include <format>
#include <memory>

namespace xournal
{
    namespace
    {
        // Larger than any font is meant to be (the renderers would have to lay out glyphs of that height).
        constexpr double MaxFontSize = 1000;

        // Pango style words that may end a font description.
        constexpr std::array<std::wstring_view, 17> StyleWords{ L"Bold", L"Italic", L"Oblique", L"Normal", L"Regular",
            L"Light", L"Medium", L"Semi-Bold", L"SemiBold", L"Ultra-Bold", L"Heavy", L"Thin", L"Book", L"Condensed",
            L"Semi-Condensed", L"Expanded", L"Small-Caps" };

        bool IsStyleWord(std::wstring_view word)
        {
            for (auto style : StyleWords)
            {
                if (EqualsIgnoreCase(style, word))
                {
                    return true;
                }
            }
            return false;
        }

        bool ContainsIgnoreCase(std::wstring_view text, std::wstring_view part)
        {
            for (size_t i = 0; i + part.size() <= text.size(); ++i)
            {
                if (EqualsIgnoreCase(text.substr(i, part.size()), part))
                {
                    return true;
                }
            }
            return false;
        }

        bool IsParagraphBreak(wchar_t c)
        {
            return c == L'\n' || c == L'\r' || c == L'\u0085' || c == L' ' || c == L' ';
        }

        struct BreakIteratorDeleter
        {
            void operator()(UBreakIterator* it) const { ubrk_close(it); }
        };
    }

    bool XoppText::IsText(XmlElement const& e) { return e.Name() == L"text" || e.Name() == L"link"; }

    XoppText XoppText::Read(XmlElement const& e)
    {
        XoppText text;
        std::wstring font = e.AttributeOr(L"font", L"Sans");
        auto words = Split(font, L' ');
        while (words.size() > 1 && IsStyleWord(words.back()))
        {
            auto word = words.back();
            text.bold = text.bold || ContainsIgnoreCase(word, L"Bold") || EqualsIgnoreCase(word, L"Heavy");
            text.italic = text.italic || EqualsIgnoreCase(word, L"Italic") || EqualsIgnoreCase(word, L"Oblique");
            words.pop_back();
        }
        for (size_t i = 0; i < words.size(); ++i)
        {
            text.family += (i > 0 ? L" " : L"") + std::wstring(words[i]);
        }
        while (!text.family.empty() && text.family.back() == L',')
        {
            text.family.pop_back();
        }
        if (words.empty())
        {
            text.family = L"Sans";
        }

        auto matrix = XoppFormat::ReadMatrix(e);
        text.transform = matrix ? *matrix
            : Matrix::Translation(XoppFormat::ReadNumber(e, L"x", 0), XoppFormat::ReadNumber(e, L"y", 0));
        auto wrap = XoppFormat::ReadNumber(e.Attribute(L"wrap"));
        if (wrap && *wrap > 0)
        {
            text.wrap = wrap;
        }
        auto align = e.AttributeOr(L"align", L"");
        text.alignment = align == L"center" ? TextAlignment::Center : align == L"right" ? TextAlignment::Right : TextAlignment::Left;
        auto justify = e.AttributeOr(L"justify", L"");
        text.justify = justify == L"true" || justify == L"1";
        double size = XoppFormat::ReadNumber(e, L"size", 12);
        text.size = size > 0 && size <= MaxFontSize ? size : 12;
        text.color = ParseColor(e.Attribute(L"color"), Colors::Black);
        text.content = e.Value();
        return text;
    }

    std::vector<std::wstring> XoppText::Paragraphs() const
    {
        std::vector<std::wstring> paragraphs(1);
        for (size_t i = 0; i < content.size(); ++i)
        {
            wchar_t c = content[i];
            if (c == L'\r' && i + 1 < content.size() && content[i + 1] == L'\n')
            {
                continue; // CR LF is one break
            }
            if (IsParagraphBreak(c))
            {
                paragraphs.emplace_back();
            }
            else
            {
                paragraphs.back() += c;
            }
        }
        return paragraphs;
    }

    std::wstring XoppText::WindowsFamily() const
    {
        std::wstring lower = family;
        for (auto& c : lower)
        {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower == L"sans" || lower == L"sans-serif")
            return L"Arial";
        if (lower == L"serif")
            return L"Times New Roman";
        if (lower == L"monospace" || lower == L"mono")
            return L"Courier New";
        return family;
    }

    size_t XoppText::CharacterCount(std::wstring_view text)
    {
        if (text.empty())
        {
            return 0;
        }
        UErrorCode status = U_ZERO_ERROR;
        std::unique_ptr<UBreakIterator, BreakIteratorDeleter> it(ubrk_open(UBRK_CHARACTER, nullptr,
            reinterpret_cast<UChar const*>(text.data()), static_cast<int32_t>(text.size()), &status));
        if (U_FAILURE(status))
        {
            ThrowHr(std::format("ubrk_open (ICU error {})", static_cast<int>(status)), E_FAIL);
        }
        size_t count = 0;
        for (int32_t pos = ubrk_next(it.get()); pos != UBRK_DONE; pos = ubrk_next(it.get()))
        {
            ++count;
        }
        return count;
    }
}
