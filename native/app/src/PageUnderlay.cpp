#include "PageUnderlay.h"

#include "xournal/base/Error.h"
#include "xournal/model/BackgroundRuling.h"
#include "xournal/model/Strokes.h"

#include <wrl/client.h>

#include <variant>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace xournal::app::PageUnderlay
{
    namespace
    {
        D2D1_COLOR_F ColorOf(Color c)
        {
            return { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
        }

        D2D1_POINT_2F P(Point p) { return { static_cast<float>(p.x), static_cast<float>(p.y) }; }

        ComPtr<ID2D1Factory> FactoryOf(ID2D1DeviceContext* dc)
        {
            ComPtr<ID2D1Factory> factory;
            dc->GetFactory(&factory);
            return factory;
        }

        ComPtr<ID2D1PathGeometry> Polyline(ID2D1Factory* factory, std::vector<Point> const& points, bool closedFilled)
        {
            ComPtr<ID2D1PathGeometry> path;
            Check("ID2D1Factory::CreatePathGeometry", factory->CreatePathGeometry(&path));
            ComPtr<ID2D1GeometrySink> sink;
            Check("ID2D1PathGeometry::Open", path->Open(&sink));
            sink->BeginFigure(P(points[0]), closedFilled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < points.size(); ++i)
            {
                sink->AddLine(P(points[i]));
            }
            sink->EndFigure(closedFilled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            Check("ID2D1GeometrySink::Close", sink->Close());
            return path;
        }

        void DrawBackground(XoppPage const& page, ID2D1DeviceContext* dc)
        {
            ComPtr<ID2D1SolidColorBrush> brush;
            // PDF and picture backgrounds show white until their renderers come (phase 4).
            Color paper = page.IsPdf() || page.IsImageBackground() ? Colors::White : page.BackgroundColor();
            Check("CreateSolidColorBrush", dc->CreateSolidColorBrush(ColorOf(paper), &brush));
            dc->FillRectangle({ 0, 0, static_cast<float>(page.Width()), static_cast<float>(page.Height()) }, brush.Get());
            if (page.IsPdf() || page.IsImageBackground())
            {
                return;
            }
            for (auto const& ruling : BackgroundRuling::For(page.Style(), page.BackgroundConfig(), paper, page.Width(), page.Height()))
            {
                brush->SetColor(ColorOf(ruling.color));
                float width = static_cast<float>(ruling.width);
                for (auto const& [from, to] : ruling.lines)
                {
                    dc->DrawLine(P(from), P(to), brush.Get(), width);
                }
                for (auto const& dot : ruling.dots)
                {
                    dc->FillEllipse({ P(dot), width / 2, width / 2 }, brush.Get());
                }
            }
        }

        // The stroke's ends and dashes; `drawnBefore`: the length drawn before this line (a pressure stroke's dashes run
        // on across its segments).
        ComPtr<ID2D1StrokeStyle> StyleFor(ID2D1Factory* factory, StrokeShape const& shape, double lineWidth, double drawnBefore)
        {
            D2D1_CAP_STYLE cap = shape.cap == StrokeCap::Butt ? D2D1_CAP_STYLE_FLAT
                : shape.cap == StrokeCap::Square ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_ROUND;
            auto dashes = shape.DashesFor(lineWidth);
            std::vector<float> pattern;
            if (dashes)
            {
                for (double d : *dashes) pattern.push_back(static_cast<float>(d));
            }
            D2D1_STROKE_STYLE_PROPERTIES properties{ cap, cap, cap, D2D1_LINE_JOIN_ROUND, 10,
                dashes ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID, static_cast<float>(drawnBefore / lineWidth) };
            ComPtr<ID2D1StrokeStyle> style;
            Check("CreateStrokeStyle", factory->CreateStrokeStyle(properties, pattern.empty() ? nullptr : pattern.data(),
                static_cast<UINT32>(pattern.size()), &style));
            return style;
        }

        void DrawShape(StrokeShape const& shape, ID2D1DeviceContext* dc, ID2D1Factory* factory)
        {
            ComPtr<ID2D1SolidColorBrush> brush;
            Check("CreateSolidColorBrush", dc->CreateSolidColorBrush(ColorOf(shape.color), &brush));
            for (auto const& part : shape.Parts())
            {
                if (auto fill = std::get_if<StrokeFill>(&part))
                {
                    Color c = shape.color;
                    c.a = static_cast<std::uint8_t>(shape.fill * c.a / 255); // upstream: the fill's alpha over the color's
                    brush->SetColor(ColorOf(c));
                    dc->FillGeometry(Polyline(factory, fill->points, true).Get(), brush.Get());
                    brush->SetColor(ColorOf(shape.color));
                }
                else if (auto dot = std::get_if<StrokeDot>(&part))
                {
                    float half = static_cast<float>(dot->size / 2);
                    if (dot->square)
                        dc->FillRectangle({ P(dot->center).x - half, P(dot->center).y - half, P(dot->center).x + half,
                            P(dot->center).y + half }, brush.Get());
                    else
                        dc->FillEllipse({ P(dot->center), half, half }, brush.Get());
                }
                else if (auto line = std::get_if<StrokeLine>(&part))
                {
                    auto style = StyleFor(factory, shape, line->width, line->dashOffset);
                    dc->DrawGeometry(Polyline(factory, line->points, false).Get(), brush.Get(), static_cast<float>(line->width), style.Get());
                }
            }
        }
    }

    void Draw(XoppPage const& page, ID2D1DeviceContext* dc, inkpages::RectF area)
    {
        dc->PushAxisAlignedClip({ area.left, area.top, area.right, area.bottom }, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DrawBackground(page, dc);
        auto factory = FactoryOf(dc);
        for (auto const& element : page.DrawnStrokes())
        {
            if (auto shape = Strokes::ReadShape(*element))
            {
                DrawShape(*shape, dc, factory.Get());
            }
        }
        dc->PopAxisAlignedClip();
    }
}
