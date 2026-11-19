#include "xournal/model/XoppFormat.h"

#include "xournal/base/Text.h"

#include <charconv>
#include <cmath>
#include <string>

namespace xournal::XoppFormat
{
    namespace
    {
        // from_chars reads ASCII; a number of the file never has other characters.
        bool ToAscii(std::wstring_view text, std::string& out)
        {
            out.clear();
            out.reserve(text.size());
            for (wchar_t c : text)
            {
                if (c > 0x7F)
                {
                    return false;
                }
                out.push_back(static_cast<char>(c));
            }
            return true;
        }

        // .NET accepts a leading '+', from_chars does not.
        std::string_view WithoutPlus(std::string_view s)
        {
            if (!s.empty() && s[0] == '+')
            {
                s.remove_prefix(1);
            }
            return s;
        }
    }

    std::optional<double> ParseDouble(std::wstring_view text)
    {
        std::string ascii;
        if (!ToAscii(Trim(text), ascii))
        {
            return std::nullopt;
        }
        std::string_view s = WithoutPlus(ascii);
        if (s.empty() || s[0] == '+' || (s.size() > 1 && s[0] == '-' && s[1] == '+'))
        {
            return std::nullopt;
        }
        double value = 0;
        auto [ptr, error] = std::from_chars(s.data(), s.data() + s.size(), value, std::chars_format::general);
        if (error == std::errc::result_out_of_range)
        {
            return std::nullopt; // "1e400": not a usable number
        }
        if (error != std::errc{} || ptr != s.data() + s.size())
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<int> ParseInt(std::wstring_view text)
    {
        std::string ascii;
        if (!ToAscii(Trim(text), ascii))
        {
            return std::nullopt;
        }
        std::string_view s = WithoutPlus(ascii);
        int value = 0;
        auto [ptr, error] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (s.empty() || error != std::errc{} || ptr != s.data() + s.size())
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<double> ReadNumber(std::wstring_view text)
    {
        auto value = ParseDouble(text);
        if (!value || !(std::abs(*value) <= NumberLimit)) // also false for NaN
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<double> ReadNumber(std::wstring const* text)
    {
        return text ? ReadNumber(std::wstring_view(*text)) : std::nullopt;
    }

    double ReadNumber(XmlElement const& e, std::wstring_view name, double fallback)
    {
        return ReadNumber(e.Attribute(name)).value_or(fallback);
    }

    std::optional<Matrix> ReadMatrix(XmlElement const& e)
    {
        auto text = e.Attribute(L"matrix");
        if (!text)
        {
            return std::nullopt;
        }
        auto parts = Split(*text, L' ');
        if (parts.size() != 6)
        {
            return std::nullopt;
        }
        double v[6];
        for (int i = 0; i < 6; ++i)
        {
            auto number = ReadNumber(parts[i]);
            if (!number)
            {
                return std::nullopt;
            }
            v[i] = *number;
        }
        return Matrix{ v[0], v[1], v[2], v[3], v[4], v[5] };
    }

    void WriteMatrix(XmlElement& e, Matrix const& m)
    {
        e.SetAttribute(L"matrix", FormatNumber(m.m11, 6) + L" " + FormatNumber(m.m12, 6) + L" " + FormatNumber(m.m21, 6) +
            L" " + FormatNumber(m.m22, 6) + L" " + FormatNumber(m.m31, 6) + L" " + FormatNumber(m.m32, 6));
    }

    std::wstring FormatNumber(double value, int decimals)
    {
        char buffer[400];
        auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, decimals);
        if (error != std::errc{})
        {
            // Only non-finite values or absurd magnitudes get here; shortest form keeps them readable.
            end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
        }
        std::string s(buffer, end);
        if (s.find('.') != std::string::npos)
        {
            while (s.back() == '0')
            {
                s.pop_back();
            }
            if (s.back() == '.')
            {
                s.pop_back();
            }
        }
        if (s == "-0")
        {
            s = "0";
        }
        return std::wstring(s.begin(), s.end());
    }

    std::wstring FormatShortest(double value)
    {
        char buffer[64];
        auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
        return std::wstring(buffer, end);
    }
}
