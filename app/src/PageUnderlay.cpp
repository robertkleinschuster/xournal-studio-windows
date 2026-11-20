#include "PageUnderlay.h"

#include "Direct2D.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/model/BackgroundRuling.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

#include <wrl/client.h>

#include <chrono>
#include <format>
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


        auto const processStart = std::chrono::steady_clock::now();
        constexpr int KeepPages = 6; // bitmaps of pages this far from the page in view stay
    }

    PageUnderlay::PageUnderlay(RenderQueue& queue, XoppDocument const& doc, std::uint64_t generation,
        std::function<void(int)> invalidate, std::function<void(std::set<int> const&)> invalidateBatch,
        FilePictures const& pictures, std::function<void()> pdfLoaded)
        : queue(queue), generation(generation), invalidateBatch(std::move(invalidateBatch)), pdfLoaded(std::move(pdfLoaded)),
          pdf(std::make_shared<PdfBackground>(queue, generation, this->invalidateBatch)),
          bitmaps(std::make_shared<PictureBitmaps>(queue, generation, invalidate)),
          pictures(pictures)
    {
        pdf->loaded = this->pdfLoaded;
        if (doc.PdfPath())
        {
            pdf->Load(*doc.PdfPath());
        }
    }

    void PageUnderlay::LoadPdf(std::filesystem::path const& file)
    {
        pdf = std::make_shared<PdfBackground>(queue, generation, invalidateBatch);
        pdf->loaded = pdfLoaded;
        pdf->Load(file); // its pages are drawn again as their bitmaps arrive
    }

    void PageUnderlay::Remap(inkpages::PageMap const& map)
    {
        pdf->Remap(map);
    }

    void PageUnderlay::Trim(int focus, std::vector<int> const& drawing)
    {
        pdf->Trim(focus, KeepPages, drawing);
    }

    void PageUnderlay::DrawBackground(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area,
        float pixelsPerUnit)
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
            pdf->Draw(dc, page, content.PdfPageIndex(), size, { area.left, area.top, area.right, area.bottom }, pixelsPerUnit);
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

    namespace
    {
        // The page's clip for the drawing, popped whatever happens while drawing.
        class ClipGuard
        {
        public:
            ClipGuard(ID2D1DeviceContext* dc, inkpages::RectF area) : dc(dc)
            {
                dc->PushAxisAlignedClip({ area.left, area.top, area.right, area.bottom }, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            }
            ~ClipGuard() { dc->PopAxisAlignedClip(); }
            ClipGuard(ClipGuard const&) = delete;
            ClipGuard& operator=(ClipGuard const&) = delete;

        private:
            ID2D1DeviceContext* dc;
        };
    }

    void PageUnderlay::Draw(int page, XoppPage const& content, ID2D1DeviceContext* dc, inkpages::RectF area, float pixelsPerUnit)
    {
        auto start = std::chrono::steady_clock::now();
        struct Timing
        {
            std::chrono::steady_clock::time_point start;
            int page;
            inkpages::RectF area;
            float pixelsPerUnit;
            ~Timing()
            {
                auto now = std::chrono::steady_clock::now();
                Log(std::format("underlay: page {} drawn ({:.0f}x{:.0f} units at {:.3f} px/unit) in {} ms, t={} ms", page + 1,
                    area.right - area.left, area.bottom - area.top, pixelsPerUnit,
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - processStart).count()));
            }
        } timing{ start, page, area, pixelsPerUnit };
        ClipGuard clip(dc, area);
        D2D1_MATRIX_3X2_F pageTransform;
        dc->GetTransform(&pageTransform);
        // One element that cannot be drawn (a damaged picture, a font) is logged and left out; the others are drawn.
        auto guarded = [&](char const* what, auto&& draw)
        {
            try
            {
                draw();
            }
            catch (...)
            {
                LogException(std::format("underlay: page {}: {}", page + 1, what));
            }
            dc->SetTransform(pageTransform); // an element that threw while placed by its own transform
        };
        guarded("background", [&] { DrawBackground(page, content, dc, area, pixelsPerUnit); });
        auto factory = FactoryOf(dc);
        std::set<XmlElement const*> drawnSet;
        for (auto const& e : content.DrawnStrokes()) drawnSet.insert(e.get());
        for (auto const& layer : content.Layers())
        {
            for (auto const& element : layer->Elements())
            {
                if (drawnSet.contains(element.get()))
                {
                    guarded("a stroke", [&] { if (auto shape = Strokes::ReadShape(*element)) DrawShape(*shape, dc, factory.Get()); });
                }
                else if (XoppImage::IsImage(*element))
                {
                    guarded("a picture", [&] { DrawPicture(page, element, dc, pixelsPerUnit); });
                }
                else if (XoppText::IsText(*element) && !pictures.IsEditing(element.get()))
                {
                    // The text the Text tool edits is the library's meanwhile; a dragged one shows its preview.
                    guarded("a text", [&] { texts.Draw(dc, XoppText::Read(*element), pictures.PreviewOf(element.get()).value_or(Matrix{})); });
                }
            }
        }
        if (page == highlight.page)
        {
            guarded("the search hit", [&] { DrawMark(dc, highlight); });
        }
        if (page == textSelection.page)
        {
            guarded("the selected PDF text", [&] { DrawMark(dc, textSelection); });
        }
    }

    void PageUnderlay::SetHighlight(int page, std::vector<Rect> rects)
    {
        highlight = { page, std::move(rects) };
    }

    void PageUnderlay::SetTextSelection(int page, std::vector<Rect> rects)
    {
        textSelection = { page, std::move(rects) };
    }

    void PageUnderlay::DrawMark(ID2D1DeviceContext* dc, Mark const& mark)
    {
        // The system's selection color, see-through so the text stays readable (as a text marker over the page).
        COLORREF color = GetSysColor(COLOR_HIGHLIGHT);
        ComPtr<ID2D1SolidColorBrush> brush;
        Check("CreateSolidColorBrush (mark)", dc->CreateSolidColorBrush(D2D1::ColorF(GetRValue(color) / 255.0f,
            GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, 0.35f), &brush));
        for (auto const& r : mark.rects)
        {
            dc->FillRectangle(D2D1::RectF(static_cast<float>(r.Left()) - 1, static_cast<float>(r.Top()) - 1,
                static_cast<float>(r.Right()) + 1, static_cast<float>(r.Bottom()) + 1), brush.Get());
        }
    }
}
