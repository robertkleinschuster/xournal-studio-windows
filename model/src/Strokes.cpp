#include "xournal/model/Strokes.h"

#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"

#include <algorithm>

namespace xournal::Strokes
{
    namespace
    {
        constexpr std::uint8_t HighlighterAlpha = 0x7F;

        struct StrokePoints
        {
            std::vector<Point> points;
            std::vector<std::wstring> after; // what follows the readable points, as read
        };

        // The one reader of a stroke's points (display, ink, bounds, export, Transform): coordinate pairs by the file's
        // number rule up to the first damaged number - so point i keeps the width of segment i.
        StrokePoints ReadPoints(std::wstring const& text)
        {
            auto tokens = SplitWhitespace(text);
            StrokePoints result;
            result.points.reserve(tokens.size() / 2);
            size_t i = 0;
            for (; i + 1 < tokens.size(); i += 2)
            {
                auto x = XoppFormat::ReadNumber(tokens[i]);
                auto y = XoppFormat::ReadNumber(tokens[i + 1]);
                if (!x || !y)
                {
                    break;
                }
                result.points.push_back({ *x, *y });
            }
            for (; i < tokens.size(); ++i)
            {
                result.after.emplace_back(tokens[i]);
            }
            return result;
        }

        // Widths and dashes by the file's number rule; the list ends at the first damaged one.
        std::vector<double> ParseNumbers(std::wstring_view text, size_t* tokenCount = nullptr)
        {
            auto tokens = SplitWhitespace(text);
            if (tokenCount)
            {
                *tokenCount = tokens.size();
            }
            std::vector<double> result;
            for (auto token : tokens)
            {
                auto v = XoppFormat::ReadNumber(token);
                if (!v)
                {
                    break;
                }
                result.push_back(*v);
            }
            return result;
        }

        // The stroke's widths as upstream's XmlParser reads them: the first number of "width" is the stroke's width, the
        // per-segment widths follow it in "width" (Xournal, Xournal++) - or come from "pressures" (MrWriter writes them
        // there, one per point; upstream takes them as the widths and drops the rest of "width").
        std::wstring WidthText(XmlElement const& e)
        {
            std::wstring width = e.AttributeOr(L"width", L"1");
            auto pressures = e.Attribute(L"pressures");
            if (!pressures)
            {
                return width;
            }
            auto tokens = SplitWhitespace(width);
            std::wstring text = tokens.empty() ? std::wstring(L"1") : std::wstring(tokens[0]);
            return text + L" " + *pressures;
        }

        // Any line style (also one nobody can draw dashed): such a stroke is never rewritten as ink, which would drop the
        // attribute. Solid strokes carry none ("plain" is upstream's name for solid).
        bool IsLineStyle(std::wstring const* style) { return style && !style->empty() && *style != L"plain"; }

        // Filled, with a line style or with other than round ends: what Windows ink cannot draw.
        bool IsStyled(XmlElement const& e)
        {
            auto fill = XoppFormat::ParseInt(e.AttributeOr(L"fill", L""));
            auto cap = e.Attribute(L"capStyle");
            return (fill && *fill >= 0) || IsLineStyle(e.Attribute(L"style")) ||
                (cap && (*cap == L"butt" || *cap == L"square"));
        }

        // A number the app cannot read (not a number, beyond any page, a stray coordinate): the stroke is shown as far as
        // it can be read and never rewritten as ink or moved, so nothing of it is lost or misplaced.
        bool IsDamaged(StrokePoints const& points, std::vector<double>& widths, std::wstring const& widthText)
        {
            size_t tokens = 0;
            widths = ParseNumbers(widthText, &tokens);
            return !points.after.empty() || tokens != widths.size();
        }

        // A dash pattern needs lengths of 0 or more, not all 0, and a gap: the pattern repeats (an odd one with on and
        // off swapped), so without a positive length in an "off" place it is solid. Zero "on" lengths are dots with round
        // ends, as upstream draws them.
        std::vector<double> Drawable(std::vector<double> dashes)
        {
            std::vector<double> cycle = dashes;
            if (dashes.size() % 2 == 1)
            {
                cycle.insert(cycle.end(), dashes.begin(), dashes.end());
            }
            bool allNonNegative = std::all_of(dashes.begin(), dashes.end(), [](double d) { return d >= 0; });
            bool anyPositive = std::any_of(dashes.begin(), dashes.end(), [](double d) { return d > 0; });
            bool gap = false;
            for (size_t k = 1; k < cycle.size(); k += 2)
            {
                gap = gap || cycle[k] > 0;
            }
            return allNonNegative && anyPositive && gap ? dashes : std::vector<double>{};
        }

        std::optional<StrokeInk> Build(XmlElement const& e, std::vector<Point> const& coords, std::vector<double> const& widths)
        {
            if (coords.empty())
            {
                return std::nullopt;
            }
            double width = !widths.empty() && widths[0] > 0 ? widths[0] : 1;
            bool hasPressure = widths.size() > 1;
            // Xournal's width is the width at full pressure; strokes written before 2026-11-18 have wider segments, so
            // the widest of all is taken - of the segments the stroke has (upstream drops widths beyond them, such as
            // the last point's pressure MrWriter writes).
            double size = width;
            if (hasPressure)
            {
                auto used = widths.begin() + static_cast<std::ptrdiff_t>(std::min(widths.size(), std::max<size_t>(coords.size(), 2)));
                size = std::max(width, *std::max_element(widths.begin() + 1, used)) / InkWidth::FullPressureFactor;
            }

            StrokeInk ink;
            ink.look.color = ParseColor(e.Attribute(L"color"), Colors::Black);
            ink.look.color.a = 0xFF;
            ink.look.size = size;
            ink.look.ignorePressure = !hasPressure;
            ink.look.highlighter = e.AttributeOr(L"tool", L"") == L"highlighter";

            ink.samples.reserve(coords.size() + 1);
            for (size_t i = 0; i < coords.size(); ++i)
            {
                float pressure = 0.5f;
                if (hasPressure)
                {
                    // widths[i + 1] is the width of segment i; the last point has none.
                    double w = widths[std::min(i + 1, widths.size() - 1)];
                    pressure = static_cast<float>(std::clamp(InkWidth::PressureFor(w / size), 0.01, 1.0));
                }
                ink.samples.push_back({ coords[i], pressure });
            }
            if (coords.size() == 1)
            {
                ink.samples.push_back(ink.samples[0]); // a dot: Windows ink needs two points
            }
            return ink;
        }
    }

    std::optional<StrokeShape> ReadShape(XmlElement const& e)
    {
        auto points = ReadPoints(e.Value()).points;
        if (points.empty())
        {
            return std::nullopt;
        }
        auto widths = ParseNumbers(WidthText(e));
        // No positive width: a filled or styled stroke has no line (upstream: only its filling); any other is drawn
        // 1 wide, as the ink - also a damaged one. Missing: 1.
        double width = widths.empty() ? 1 : widths[0] > 0 ? widths[0] : IsStyled(e) ? 0 : 1;

        StrokeShape shape;
        shape.color = ParseColor(e.Attribute(L"color"), Colors::Black);
        shape.highlighter = e.AttributeOr(L"tool", L"") == L"highlighter";
        shape.width = width;
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            shape.segmentWidths.push_back(widths.size() > 1 ? widths[std::min(i + 1, widths.size() - 1)] : width);
        }
        shape.points = std::move(points);
        shape.fill = XoppFormat::ParseInt(e.AttributeOr(L"fill", L"")).value_or(-1);
        shape.dashes = Dashes(e.Attribute(L"style"));
        auto cap = e.AttributeOr(L"capStyle", L"");
        shape.cap = cap == L"butt" ? StrokeCap::Butt : cap == L"square" ? StrokeCap::Square : StrokeCap::Round;
        return shape;
    }

    std::vector<double> Dashes(std::wstring const* style)
    {
        if (!style)
        {
            return {};
        }
        if (*style == L"dash")
            return { 6, 3 };
        if (*style == L"dashdot")
            return { 6, 3, 0.5, 3 };
        if (*style == L"dot")
            return { 0.5, 3 };
        if (style->starts_with(L"cust: "))
            return Drawable(ParseNumbers(std::wstring_view(*style).substr(6)));
        return {};
    }

    bool NeedsOwnDrawing(XmlElement const& e)
    {
        if (IsStyled(e))
        {
            return true;
        }
        std::vector<double> widths;
        return IsDamaged(ReadPoints(e.Value()), widths, WidthText(e));
    }

    std::optional<StrokeInk> InkOf(XmlElement const& e)
    {
        if (IsStyled(e))
        {
            return std::nullopt;
        }
        auto points = ReadPoints(e.Value());
        std::vector<double> widths;
        if (IsDamaged(points, widths, WidthText(e)))
        {
            return std::nullopt;
        }
        return Build(e, points.points, widths);
    }

    std::optional<StrokeInk> FromXml(XmlElement const& e)
    {
        return Build(e, ReadPoints(e.Value()).points, ParseNumbers(WidthText(e)));
    }

    XmlElementPtr ToXml(StrokeInk const& ink, Matrix const& transform)
    {
        using XoppFormat::FormatNumber;
        std::wstring coords;
        coords.reserve(ink.samples.size() * 16);
        for (auto const& sample : ink.samples)
        {
            Point p = transform.Apply(sample.position);
            if (!coords.empty())
            {
                coords += L' ';
            }
            coords += FormatNumber(p.x) + L" " + FormatNumber(p.y);
        }

        double size = ink.look.size;
        std::wstring widths = FormatNumber(ink.look.ignorePressure ? size : size * InkWidth::FullPressureFactor);
        if (!ink.look.ignorePressure)
        {
            for (size_t i = 0; i + 1 < ink.samples.size(); ++i)
            {
                widths += L" " + FormatNumber(size * InkWidth::Factor(ink.samples[i].pressure));
            }
        }

        Color color = ink.look.color;
        color.a = ink.look.highlighter ? HighlighterAlpha : 0xFF;
        auto e = XmlElement::Make(L"stroke", {
            { L"tool", ink.look.highlighter ? L"highlighter" : L"pen" },
            { L"color", FormatColor(color) },
            { L"width", widths },
        });
        e->SetValue(std::move(coords));
        return e;
    }

    void Transform(XmlElement& e, Matrix const& m)
    {
        using XoppFormat::FormatNumber;
        auto [points, after] = ReadPoints(e.Value());
        std::wstring moved;
        for (auto const& p : points)
        {
            Point q = m.Apply(p);
            if (!moved.empty())
            {
                moved += L' ';
            }
            moved += FormatNumber(q.x) + L" " + FormatNumber(q.y);
        }
        for (auto const& token : after)
        {
            if (!moved.empty())
            {
                moved += L' ';
            }
            moved += token;
        }
        e.SetValue(std::move(moved));
    }
}
