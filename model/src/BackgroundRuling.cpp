#include "xournal/model/BackgroundRuling.h"

#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"

#include <charconv>
#include <cmath>
#include <map>
#include <optional>

namespace xournal::BackgroundRuling
{
    namespace
    {
        constexpr double HeaderSize = 80, FooterSize = 60;

        constexpr Color Rgb(std::uint32_t v)
        {
            return { 0xFF, static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v) };
        }

        constexpr Color Silver = Rgb(0xbdbdbd), DarkSlateGray = Rgb(0x434343), DodgerBlue = Rgb(0x40a0ff),
            DeepPink = Rgb(0xff0080), MidnightBlue = Rgb(0x220080), Black = Rgb(0x000000), White = Rgb(0xffffff);

        // "key=value,key=value" (upstream BackgroundConfig).
        class Config
        {
        public:
            explicit Config(std::wstring const* text)
            {
                if (!text)
                {
                    return;
                }
                for (auto part : Split(*text, L','))
                {
                    size_t eq = part.rfind(L'=');
                    if (eq != std::wstring_view::npos && eq > 0)
                    {
                        values[std::wstring(Trim(part.substr(0, eq)))] = std::wstring(Trim(part.substr(eq + 1)));
                    }
                }
            }

            std::optional<double> Number(std::wstring const& key) const
            {
                auto it = values.find(key);
                return it == values.end() ? std::nullopt : XoppFormat::ParseDouble(it->second);
            }

            // AARRGGBB as upstream's Color(uint32); six digits are taken as opaque RRGGBB.
            std::optional<Color> HexColor(std::wstring const& key) const
            {
                auto it = values.find(key);
                if (it == values.end())
                {
                    return std::nullopt;
                }
                std::wstring_view s = it->second;
                while (!s.empty() && s.front() == L'#')
                {
                    s.remove_prefix(1);
                }
                if (StartsWithIgnoreCase(s, L"0x"))
                {
                    s.remove_prefix(2); // std::stoul(..., 16) as upstream accepts the prefix
                }
                std::string ascii;
                for (wchar_t c : s)
                {
                    if (c > 0x7F)
                    {
                        return std::nullopt;
                    }
                    ascii.push_back(static_cast<char>(c));
                }
                std::uint32_t v = 0;
                auto [ptr, error] = std::from_chars(ascii.data(), ascii.data() + ascii.size(), v, 16);
                if (ascii.empty() || error != std::errc{} || ptr != ascii.data() + ascii.size())
                {
                    return std::nullopt;
                }
                if (ascii.size() <= 6)
                {
                    v |= 0xFF000000;
                }
                return Color{ static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
                    static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v) };
            }

        private:
            std::map<std::wstring, std::wstring> values;
        };

        // Indices i with i * step in [margin, length - margin] (upstream getIndexBounds for the whole page).
        std::pair<int, int> Indices(double step, double margin, double length)
        {
            return { static_cast<int>(std::ceil(margin / step)), static_cast<int>(std::floor((length - margin) / step)) };
        }

        RulingLines Ruled(Config const& c, Color color, double lineWidth, double width, double height)
        {
            double spacing = c.Number(L"r1").value_or(24);
            RulingLines ruling{ color, lineWidth, {}, {} };
            auto [min, max] = Indices(spacing, 0, height - HeaderSize - FooterSize);
            for (int i = std::max(0, min); i <= max; ++i)
            {
                double y = HeaderSize + i * spacing;
                ruling.lines.push_back({ { 0, y }, { width, y } });
            }
            return ruling;
        }

        RulingLines Staves(Color color, double lineWidth, double width, double height)
        {
            constexpr double Margin = 50, StavesSpacing = 40, LinesSpacing = 5;
            double staffHeight = lineWidth + 4 * LinesSpacing;
            double offset = StavesSpacing + staffHeight + 4 * lineWidth; // upstream keeps "+ 4 * lineWidth" for old files
            RulingLines ruling{ color, lineWidth, {}, {} };
            auto [min, max] = Indices(offset, 0, height - HeaderSize - FooterSize);
            for (int i = std::max(0, min); i <= max; ++i)
            {
                double top = HeaderSize + i * offset;
                for (int k = 0; k < 5; ++k)
                {
                    ruling.lines.push_back({ { Margin, top + k * LinesSpacing }, { width - Margin, top + k * LinesSpacing } });
                }
                ruling.lines.push_back({ { width - Margin, top + 4 * LinesSpacing }, { width - Margin, top } });
                ruling.lines.push_back({ { Margin, top }, { Margin, top + 4 * LinesSpacing } });
            }
            return ruling;
        }

        std::vector<RulingLines> Graph(Config const& c, Color color, double lineWidth, double width, double height)
        {
            double square = c.Number(L"r1").value_or(14.17);
            double margin = c.Number(L"m1").value_or(0);
            int bold = static_cast<int>(c.Number(L"bli").value_or(0));
            double boldWidth = c.Number(L"blw").value_or(1.5);
            bool roundMargin = margin > 0 && c.Number(L"rm").value_or(0) != 0;

            double half = 0.5 * (bold > 0 ? boldWidth : lineWidth);
            double clearance = std::max(margin, half);
            auto [minIx, maxIx] = Indices(square, clearance, width);
            auto [minIy, maxIy] = Indices(square, clearance, height);
            double minX = margin > 0 ? margin : 0, maxX = margin > 0 ? width - margin : width;
            double minY = margin > 0 ? margin : 0, maxY = margin > 0 ? height - margin : height;
            if (roundMargin)
            {
                minX = std::max(minX, square * minIx);
                maxX = std::min(maxX, square * maxIx);
                minY = std::max(minY, square * minIy);
                maxY = std::min(maxY, square * maxIy);
            }
            RulingLines thin{ color, lineWidth, {}, {} }, thick{ color, boldWidth, {}, {} };
            auto lineFor = [&](int i) -> RulingLines& { return bold > 0 && i % bold == 0 ? thick : thin; };
            if (minY < maxY)
            {
                for (int i = minIx; i <= maxIx; ++i)
                {
                    lineFor(i).lines.push_back({ { i * square, minY }, { i * square, maxY } });
                }
            }
            if (minX < maxX)
            {
                for (int i = minIy; i <= maxIy; ++i)
                {
                    lineFor(i).lines.push_back({ { minX, i * square }, { maxX, i * square } });
                }
            }
            if (bold > 0)
            {
                return { thin, thick };
            }
            return { thin };
        }

        RulingLines Dotted(Config const& c, Color color, double lineWidth, double width, double height)
        {
            double square = c.Number(L"r1").value_or(14.17);
            auto [minIx, maxIx] = Indices(square, 0.5 * lineWidth, width);
            auto [minIy, maxIy] = Indices(square, 0.5 * lineWidth, height);
            RulingLines ruling{ color, lineWidth, {}, {} };
            for (int i = minIx; i <= maxIx; ++i)
            {
                for (int j = minIy; j <= maxIy; ++j)
                {
                    ruling.dots.push_back({ i * square, j * square });
                }
            }
            return ruling;
        }

        // Upstream BaseIsometricBackgroundView + IsoDotted/IsoGraph paintGrid, for the whole page.
        RulingLines Isometric(Config const& c, Color color, double lineWidth, double width, double height, bool dots)
        {
            double triangle = c.Number(L"r1").value_or(14.17);
            double xstep = std::sqrt(3.0) / 2.0 * triangle, ystep = triangle / 2.0;
            int cols = static_cast<int>(std::floor((width - 2 * triangle) / xstep));
            int rows = static_cast<int>(std::floor((height - 2 * triangle) / ystep));
            double contentWidth = cols * xstep, contentHeight = rows * ystep;
            double x0 = (width - contentWidth) / 2, y0 = (height - contentHeight) / 2;
            auto P = [&](double x, double y) { return Point{ x0 + x, y0 + y }; };

            RulingLines ruling{ color, lineWidth, {}, {} };
            if (dots)
            {
                for (int col = 0; col <= cols; ++col)
                {
                    bool even = col % 2 == 0;
                    double offset = even ? ystep : 0;
                    int rowsInCol = even ? rows - 2 : rows;
                    for (int row = 0; row <= rowsInCol; row += 2)
                    {
                        ruling.dots.push_back(P(col * xstep, offset + row * ystep));
                    }
                }
                return ruling;
            }

            ruling.lines.push_back({ P(0, 0), P(contentWidth, 0) });
            ruling.lines.push_back({ P(0, contentHeight), P(contentWidth, contentHeight) });
            for (int col = 0; col <= cols; ++col)
            {
                ruling.lines.push_back({ P(col * xstep, 0), P(col * xstep, contentHeight) });
            }
            int hdiags = cols / 2, vdiags = rows / 2, diags = hdiags + vdiags;
            int hcorr = cols - 2 * hdiags, vcorr = rows - 2 * vdiags;
            for (int d = 0; d < diags + hcorr * vcorr; ++d)
            {
                double x1 = contentWidth, y1 = 0;
                if (d < hdiags) x1 = xstep + d * 2 * xstep; else y1 = ystep + (d - hdiags) * 2 * ystep - hcorr * ystep;
                double x2 = 0, y2 = contentHeight;
                if (d < vdiags) y2 = ystep + d * 2 * ystep; else x2 = xstep + (d - vdiags) * 2 * xstep - vcorr * xstep;
                ruling.lines.push_back({ P(x1, y1), P(x2, y2) });
            }
            for (int d = 0; d < diags; ++d)
            {
                double x1 = 0, y1 = 0;
                if (d < hdiags) x1 = contentWidth - (xstep + d * 2 * xstep) - hcorr * xstep; else y1 = ystep + (d - hdiags) * 2 * ystep;
                double x2 = contentWidth, y2 = contentHeight;
                if (d < vdiags) y2 = ystep + d * 2 * ystep + hcorr * ystep;
                else x2 = contentWidth - (xstep + (d - vdiags) * 2 * xstep) + (vcorr - hcorr) * xstep;
                ruling.lines.push_back({ P(x1, y1), P(x2, y2) });
            }
            return ruling;
        }
    }

    std::vector<RulingLines> For(BackgroundStyle style, std::wstring const* configText, Color paper, double width, double height)
    {
        Config c(configText);
        bool light = paper.r + paper.g + paper.b > 0x180; // upstream Color::isLight
        auto foreground = [&](wchar_t const* key, wchar_t const* altKey, Color normal, Color alternative)
        {
            return light ? c.HexColor(key).value_or(normal) : c.HexColor(altKey).value_or(alternative);
        };
        auto lw = [&](double fallback) { return c.Number(L"lw").value_or(fallback); };

        switch (style)
        {
        case BackgroundStyle::Ruled:
            return { Ruled(c, foreground(L"f1", L"af1", DodgerBlue, DarkSlateGray), lw(0.5), width, height) };
        case BackgroundStyle::Lined:
        {
            double lineWidth = lw(0.5);
            double margin = c.Number(L"m1").value_or(72);
            if (margin < 0)
            {
                margin += width; // a negative margin puts the line on the right
            }
            RulingLines marginLine{ foreground(L"f2", L"af2", DeepPink, MidnightBlue), lineWidth,
                { { { margin, 0 }, { margin, height } } }, {} };
            return { Ruled(c, foreground(L"f1", L"af1", DodgerBlue, DarkSlateGray), lineWidth, width, height), marginLine };
        }
        case BackgroundStyle::Staves:
            return { Staves(foreground(L"f1", L"af1", Black, White), lw(0.5), width, height) };
        case BackgroundStyle::Graph:
            return Graph(c, foreground(L"f1", L"af1", Silver, DarkSlateGray), lw(0.5), width, height);
        case BackgroundStyle::Dotted:
            return { Dotted(c, foreground(L"f1", L"af1", Silver, DarkSlateGray), lw(1.5), width, height) };
        case BackgroundStyle::IsoDotted:
            return { Isometric(c, foreground(L"f1", L"af1", Silver, DarkSlateGray), lw(1.5), width, height, true) };
        case BackgroundStyle::IsoGraph:
            return { Isometric(c, foreground(L"f1", L"af1", Silver, DarkSlateGray), lw(1.0), width, height, false) };
        default:
            return {};
        }
    }
}
