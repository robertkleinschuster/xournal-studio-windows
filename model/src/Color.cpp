#include "xournal/model/Color.h"

#include <array>
#include <charconv>
#include <format>

namespace xournal
{
    namespace
    {
        struct NamedColor
        {
            std::wstring_view name;
            Color color;
        };

        // Old Xournal's predefined colors (still read by Xournal++).
        constexpr std::array NamedColors{
            NamedColor{ L"black", { 0xFF, 0, 0, 0 } },
            NamedColor{ L"blue", { 0xFF, 0x33, 0x33, 0xcc } },
            NamedColor{ L"red", { 0xFF, 0xff, 0, 0 } },
            NamedColor{ L"green", { 0xFF, 0, 0x80, 0 } },
            NamedColor{ L"gray", { 0xFF, 0x80, 0x80, 0x80 } },
            NamedColor{ L"lightblue", { 0xFF, 0, 0xc0, 0xff } },
            NamedColor{ L"lightgreen", { 0xFF, 0, 0xff, 0 } },
            NamedColor{ L"magenta", { 0xFF, 0xff, 0, 0xff } },
            NamedColor{ L"orange", { 0xFF, 0xff, 0x80, 0 } },
            NamedColor{ L"yellow", { 0xFF, 0xff, 0xff, 0 } },
            NamedColor{ L"white", { 0xFF, 0xff, 0xff, 0xff } },
        };

        bool ParseHex(std::wstring_view digits, std::uint32_t& value)
        {
            char narrow[8];
            if (digits.size() > sizeof narrow)
            {
                return false;
            }
            for (size_t i = 0; i < digits.size(); ++i)
            {
                if (digits[i] > 0x7F)
                {
                    return false;
                }
                narrow[i] = static_cast<char>(digits[i]);
            }
            auto end = narrow + digits.size();
            auto [ptr, error] = std::from_chars(narrow, end, value, 16);
            return error == std::errc{} && ptr == end;
        }
    }

    Color ParseColor(std::wstring_view s, Color fallback)
    {
        if (s.empty())
        {
            return fallback;
        }
        std::uint32_t v = 0;
        if (s[0] == L'#' && (s.size() == 9 || s.size() == 7) && ParseHex(s.substr(1), v))
        {
            if (s.size() == 7)
            {
                v = (v << 8) | 0xFF;
            }
            return { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
                static_cast<std::uint8_t>(v >> 8) };
        }
        for (auto const& named : NamedColors)
        {
            if (named.name == s)
            {
                return named.color;
            }
        }
        return fallback;
    }

    Color ParseColor(std::wstring const* text, Color fallback)
    {
        return text ? ParseColor(std::wstring_view(*text), fallback) : fallback;
    }

    std::wstring FormatColor(Color c)
    {
        return std::format(L"#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a);
    }
}
