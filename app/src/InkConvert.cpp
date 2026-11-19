#include "InkConvert.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>

#include <vector>

using namespace winrt::Windows::UI::Input::Inking;

namespace xournal::app::InkConvert
{
    namespace
    {
        InkDrawingAttributes AttributesOf(InkLook const& look)
        {
            InkDrawingAttributes a;
            a.Color(winrt::Windows::UI::Color{ look.color.a, look.color.r, look.color.g, look.color.b });
            float size = static_cast<float>(look.size);
            a.Size({ size, size });
            a.PenTip(PenTipShape::Circle);
            a.FitToCurve(false); // Xournal++ draws polylines; curve fitting would round a rectangle's corners
            a.IgnorePressure(look.ignorePressure);
            a.DrawAsHighlighter(look.highlighter);
            return a;
        }
    }

    inkpages::InkStroke ToInkStroke(StrokeInk const& ink)
    {
        auto attributes = AttributesOf(ink.look);
        std::vector<InkPoint> points;
        points.reserve(ink.samples.size());
        for (auto const& s : ink.samples)
        {
            points.emplace_back(winrt::Windows::Foundation::Point{ static_cast<float>(s.position.x), static_cast<float>(s.position.y) },
                s.pressure);
        }
        InkStrokeBuilder builder;
        builder.SetDefaultDrawingAttributes(attributes);
        auto stroke = builder.CreateStrokeFromInkPoints(winrt::single_threaded_vector(std::move(points)),
            winrt::Windows::Foundation::Numerics::float3x2::identity());
        stroke.DrawingAttributes(attributes);
        return stroke;
    }

    StrokeInk FromInkStroke(inkpages::InkStroke const& stroke)
    {
        StrokeInk ink;
        auto a = stroke.DrawingAttributes();
        auto c = a.Color();
        ink.look.color = { c.A, c.R, c.G, c.B };
        ink.look.size = a.Size().Width;
        ink.look.ignorePressure = a.IgnorePressure();
        ink.look.highlighter = a.DrawAsHighlighter();
        auto m = stroke.PointTransform();
        Matrix transform{ m.m11, m.m12, m.m21, m.m22, m.m31, m.m32 };
        for (auto const& p : stroke.GetInkPoints())
        {
            auto at = transform.Apply({ p.Position().X, p.Position().Y });
            ink.samples.push_back({ at, p.Pressure() });
        }
        return ink;
    }
}
