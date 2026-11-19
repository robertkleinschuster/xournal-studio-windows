#include "PageUnderlay.h"

#include "Direct2D.h"

#include "xournal/base/Error.h"
#include "xournal/model/BackgroundRuling.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

#include <wrl/client.h>

#include <set>
#include <variant>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace xournal::app
{
    namespace
    {


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
            sink->BeginFigure(ToD2D(points[0]), closedFilled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < points.size(); ++i)
            {
                sink->AddLine(ToD2D(points[i]));
            }
            sink->EndFigure(closedFilled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            Check("ID2D1GeometrySink::Close", sink->Close());
            return path;
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
            Check("CreateSolidColorBrush", dc->CreateSolidColorBrush(ToD2D(shape.color), &brush));
            for (auto const& part : shape.Parts())
            {
                if (auto fill = std::get_if<StrokeFill>(&part))
                {
                    Color c = shape.color;
                    c.a = static_cast<std::uint8_t>(shape.fill * c.a / 255); // upstream: the fill's alpha over the color's
                    brush->SetColor(ToD2D(c));
                    dc->FillGeometry(Polyline(factory, fill->points, true).Get(), brush.Get());
                    brush->SetColor(ToD2D(shape.color));
                }
                else if (auto dot = std::get_if<StrokeDot>(&part))
                {
                    float half = static_cast<float>(dot->size / 2);
                    if (dot->square)
                        dc->FillRectangle({ ToD2D(dot->center).x - half, ToD2D(dot->center).y - half, ToD2D(dot->center).x + half,
                            ToD2D(dot->center).y + half }, brush.Get());
                    else
                        dc->FillEllipse({ ToD2D(dot->center), half, half }, brush.Get());
                }
                else if (auto line = std::get_if<StrokeLine>(&part))
                {
                    auto style = StyleFor(factory, shape, line->width, line->dashOffset);
                    dc->DrawGeometry(Polyline(factory, line->points, false).Get(), brush.Get(), static_cast<float>(line->width), style.Get());
                }
            }
        }


        constexpr int KeepPages = 6; // bitmaps of pages this far from the page in view stay
    }

    PageUnderlay::PageUnderlay(RenderQueue& queue, XoppDocument const& doc, std::uint64_t generation,
        std::function<void(int)> invalidate, FilePictures const& pictures)
        : pdf(std::make_shared<PdfBackground>(queue, generation, invalidate)),
          bitmaps(std::make_shared<PictureBitmaps>(queue, generation, invalidate)),
          pictures(pictures)
    {
        if (doc.PdfPath())
        {
            pdf->Load(*doc.PdfPath());
        }
    }

    void PageUnderlay::Remap(inkpages::PageMap const& map)
    {
        pdf->Remap(map);
    }

    void PageUnderlay::Trim(int focus)
    {
        pdf->Trim(focus, KeepPages);
    }

    void PageUnderlay::DrawBackground(int page, XoppPage const& content, ID2D1DeviceContext* dc, float pixelsPerUnit)
    {
        D2D1_SIZE_F size{ static_cast<float>(content.Width()), static_cast<float>(content.Height()) };
        bool pdfPage = content.IsPdf(), picture = content.IsImageBackground();
        ComPtr<ID2D1SolidColorBrush> brush;
        // Under a PDF or picture: white, as upstream shows a page whose background is missing.
        Color paper = pdfPage || picture ? Colors::White : content.BackgroundColor();
        Check("CreateSolidColorBrush", dc->CreateSolidColorBrush(ToD2D(paper), &brush));
        dc->FillRectangle({ 0, 0, size.width, size.height }, brush.Get());
        if (pdfPage)
        {
            pdf->Draw(dc, page, content.PdfPageIndex(), size, pixelsPerUnit);
            return;
        }
        if (picture)
        {
            if (content.BackgroundImagePath())
            {
                bitmaps->DrawFile(dc, *content.BackgroundImagePath(), size);
            }
            return;
        }
        for (auto const& ruling : BackgroundRuling::For(content.Style(), content.BackgroundConfig(), paper, content.Width(), content.Height()))
        {
            brush->SetColor(ToD2D(ruling.color));
            float width = static_cast<float>(ruling.width);
            for (auto const& [from, to] : ruling.lines)
            {
                dc->DrawLine(ToD2D(from), ToD2D(to), brush.Get(), width);
            }
            for (auto const& dot : ruling.dots)
            {
                dc->FillEllipse({ ToD2D(dot), width / 2, width / 2 }, brush.Get());
            }
        }
    }

    void PageUnderlay::DrawPicture(int page, XmlElementPtr const& picture, ID2D1DeviceContext* dc, float pixelsPerUnit)
    {
        auto placement = XoppImage::GetPlacement(*picture);
        Matrix transform = placement.transform;
        if (auto preview = pictures.PreviewOf(picture.get()))
        {
            transform = transform * *preview; // placed, then moved by the drag (page units)
        }
        D2D1_MATRIX_3X2_F pageTransform;
        dc->GetTransform(&pageTransform);
        dc->SetTransform(ToD2D(transform) * pageTransform);
        // Sharpness: the picture's pixels per unit of its own size (a scaled picture needs more).
        double scale = std::sqrt(std::abs(transform.m11 * transform.m22 - transform.m12 * transform.m21));
        bitmaps->DrawElement(dc, picture, page, { static_cast<float>(placement.size.width), static_cast<float>(placement.size.height) },
            static_cast<float>(pixelsPerUnit * scale));
        dc->SetTransform(pageTransform);
    }

    void PageUnderlay::Draw(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area, float pixelsPerUnit)
    {
        dc->PushAxisAlignedClip({ area.left, area.top, area.right, area.bottom }, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        DrawBackground(page, content, dc, pixelsPerUnit);
        auto factory = FactoryOf(dc);
        auto const& drawn = content.DrawnStrokes();
        std::set<XmlElement const*> drawnSet;
        for (auto const& e : drawn) drawnSet.insert(e.get());
        for (auto const& layer : content.Layers())
        {
            for (auto const& element : layer->Elements())
            {
                if (drawnSet.contains(element.get()))
                {
                    if (auto shape = Strokes::ReadShape(*element)) DrawShape(*shape, dc, factory.Get());
                }
                else if (XoppImage::IsImage(*element))
                {
                    DrawPicture(page, element, dc, pixelsPerUnit);
                }
                else if (XoppText::IsText(*element))
                {
                    texts.Draw(dc, XoppText::Read(*element));
                }
            }
        }
        dc->PopAxisAlignedClip();
    }
}
