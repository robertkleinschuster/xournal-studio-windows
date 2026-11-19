#include "ContentStream.h"

#include <charconv>
#include <cmath>

namespace xournal::pdf::detail
{
    std::string Number(double value, int decimals)
    {
        if (!std::isfinite(value))
        {
            return "0";
        }
        char buffer[64];
        auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, decimals);
        if (ec != std::errc())
        {
            return "0";
        }
        std::string s(buffer, end);
        if (s.find('.') != std::string::npos)
        {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        return s == "-0" ? "0" : s;
    }

    ContentStream& ContentStream::Raw(std::string_view s)
    {
        text.append(s);
        return *this;
    }

    ContentStream& ContentStream::Numbers(std::initializer_list<double> values, int decimals, std::string_view op)
    {
        for (double v : values)
        {
            text += Number(v, decimals);
            text += ' ';
        }
        text.append(op);
        text += '\n';
        return *this;
    }

    ContentStream& ContentStream::Transform(double a, double b, double c, double d, double e, double f)
    {
        return Numbers({ a, b, c, d, e, f }, 5, "cm");
    }

    ContentStream& ContentStream::MoveTo(Point p) { return Numbers({ p.x, p.y }, 3, "m"); }
    ContentStream& ContentStream::LineTo(Point p) { return Numbers({ p.x, p.y }, 3, "l"); }
    ContentStream& ContentStream::CurveTo(Point c1, Point c2, Point p)
    {
        return Numbers({ c1.x, c1.y, c2.x, c2.y, p.x, p.y }, 3, "c");
    }
    ContentStream& ContentStream::Rectangle(double x, double y, double width, double height)
    {
        return Numbers({ x, y, width, height }, 3, "re");
    }

    ContentStream& ContentStream::Circle(Point c, double r)
    {
        double const k = 0.5522847498307936 * r; // 4/3 (sqrt(2) - 1): a quarter circle as a cubic Bezier
        MoveTo({ c.x + r, c.y });
        CurveTo({ c.x + r, c.y + k }, { c.x + k, c.y + r }, { c.x, c.y + r });
        CurveTo({ c.x - k, c.y + r }, { c.x - r, c.y + k }, { c.x - r, c.y });
        CurveTo({ c.x - r, c.y - k }, { c.x - k, c.y - r }, { c.x, c.y - r });
        CurveTo({ c.x + k, c.y - r }, { c.x + r, c.y - k }, { c.x + r, c.y });
        return ClosePath();
    }

    ContentStream& ContentStream::Polyline(std::span<Point const> points)
    {
        if (points.empty())
        {
            return *this;
        }
        MoveTo(points[0]);
        for (size_t i = 1; i < points.size(); ++i)
        {
            LineTo(points[i]);
        }
        return *this;
    }

    ContentStream& ContentStream::StrokeColor(Color c) { return Numbers({ c.r / 255.0, c.g / 255.0, c.b / 255.0 }, 3, "RG"); }
    ContentStream& ContentStream::FillColor(Color c) { return Numbers({ c.r / 255.0, c.g / 255.0, c.b / 255.0 }, 3, "rg"); }
    ContentStream& ContentStream::LineWidth(double width) { return Numbers({ width }, 3, "w"); }

    ContentStream& ContentStream::Cap(StrokeCap cap)
    {
        switch (cap)
        {
        case StrokeCap::Butt: return Raw("0 J\n");
        case StrokeCap::Square: return Raw("2 J\n");
        default: return Raw("1 J\n");
        }
    }

    ContentStream& ContentStream::Dash(std::span<double const> lengths, double phase)
    {
        text += '[';
        for (size_t i = 0; i < lengths.size(); ++i)
        {
            if (i) text += ' ';
            text += Number(lengths[i]);
        }
        text += "] ";
        text += Number(phase);
        text += " d\n";
        return *this;
    }

    ContentStream& ContentStream::GraphicsState(std::string_view name)
    {
        text.append(name);
        text += " gs\n";
        return *this;
    }

    ContentStream& ContentStream::Draw(std::string_view name)
    {
        text.append(name);
        text += " Do\n";
        return *this;
    }
}
