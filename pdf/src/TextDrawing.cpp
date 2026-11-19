#include "TextDrawing.h"

#include "xournal/base/Error.h"

#include <d2d1.h>
#include <wrl/implements.h>

#include <cmath>
#include <format>

namespace xournal::pdf::detail
{
    namespace
    {
        // Writes DirectWrite's glyph outlines (y down, relative to the run's baseline origin) as PDF path operators.
        class OutlineSink
            : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ID2D1SimplifiedGeometrySink>
        {
        public:
            explicit OutlineSink(ContentStream& content) : content(content) {}
            bool evenOdd = false;

            STDMETHODIMP_(void) SetFillMode(D2D1_FILL_MODE mode) override { evenOdd = mode == D2D1_FILL_MODE_ALTERNATE; }
            STDMETHODIMP_(void) SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
            STDMETHODIMP_(void) BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) override { content.MoveTo({ p.x, p.y }); }
            STDMETHODIMP_(void) AddLines(D2D1_POINT_2F const* points, UINT32 count) override
            {
                for (UINT32 i = 0; i < count; ++i) content.LineTo({ points[i].x, points[i].y });
            }
            STDMETHODIMP_(void) AddBeziers(D2D1_BEZIER_SEGMENT const* beziers, UINT32 count) override
            {
                for (UINT32 i = 0; i < count; ++i)
                {
                    auto const& b = beziers[i];
                    content.CurveTo({ b.point1.x, b.point1.y }, { b.point2.x, b.point2.y }, { b.point3.x, b.point3.y });
                }
            }
            STDMETHODIMP_(void) EndFigure(D2D1_FIGURE_END) override { content.ClosePath(); }
            STDMETHODIMP Close() override { return S_OK; }

        private:
            ContentStream& content;
        };

        void DrawEmbedded(ContentStream& c, std::string const& fontName, EmbeddedFont const& font, GlyphRun const& run)
        {
            c.Raw("BT\n").Raw(fontName).Raw(" " + Number(run.emSize) + " Tf\n");
            if (!run.rightToLeft && run.offsets.empty())
            {
                // One TJ: each glyph's PDF advance is its /W width; the difference to the layout's advance (kerning,
                // justification) follows it as a TJ adjustment (in 1/1000 em, subtracted).
                c.Raw("1 0 0 -1 " + Number(run.origin.x) + " " + Number(run.origin.y) + " Tm\n[<");
                for (size_t i = 0; i < run.glyphs.size(); ++i)
                {
                    c.Raw(std::format("{:04X}", run.glyphs[i]));
                    auto w = font.widths.find(run.glyphs[i]);
                    double pdfAdvance = w != font.widths.end() ? w->second : 0;
                    double adjust = pdfAdvance - run.advances[i] * 1000.0 / run.emSize;
                    if (std::abs(adjust) > 0.05 && i + 1 < run.glyphs.size()) c.Raw(">" + Number(adjust) + "<");
                }
                c.Raw(">] TJ\n");
            }
            else
            {
                // Right-to-left or offset glyphs (e.g. justified): each glyph at its own position.
                double pen = run.origin.x;
                for (size_t i = 0; i < run.glyphs.size(); ++i)
                {
                    double advanceOffset = run.offsets.empty() ? 0 : run.offsets[i].advanceOffset;
                    double ascenderOffset = run.offsets.empty() ? 0 : run.offsets[i].ascenderOffset;
                    double x = run.rightToLeft ? pen - run.advances[i] - advanceOffset : pen + advanceOffset;
                    double y = run.origin.y - ascenderOffset;
                    c.Raw("1 0 0 -1 " + Number(x) + " " + Number(y) + " Tm <" + std::format("{:04X}", run.glyphs[i]) + "> Tj\n");
                    pen += run.rightToLeft ? -run.advances[i] : run.advances[i];
                }
            }
            c.Raw("ET\n");
        }

        void DrawOutlines(ContentStream& c, GlyphRun const& run)
        {
            c.Save().Transform(1, 0, 0, 1, run.origin.x, run.origin.y);
            auto sink = Microsoft::WRL::Make<OutlineSink>(c);
            Check("GetGlyphRunOutline", run.face->GetGlyphRunOutline(
                run.emSize, run.glyphs.data(), run.advances.data(), run.offsets.empty() ? nullptr : run.offsets.data(),
                static_cast<UINT32>(run.glyphs.size()), run.sideways, run.rightToLeft, sink.Get()));
            if (sink->evenOdd) c.FillEvenOdd();
            else c.FillNonZero();
            c.Restore();
        }
    }

    int DrawText(ContentStream& c, PageResources& resources, AlphaStates& alpha, Fonts const& fonts, XoppText const& text,
                 std::vector<GlyphRun> const& runs)
    {
        if (runs.empty())
        {
            return 0;
        }
        int outlines = 0;
        c.Save().Transform(text.transform);
        if (text.color.a < 255) c.GraphicsState(resources.State(alpha.For(255, text.color.a)));
        c.FillColor(text.color);
        for (auto const& run : runs)
        {
            if (EmbeddedFont const* font = fonts.For(run.face.Get()))
            {
                DrawEmbedded(c, resources.Font(font->dictionary), *font, run);
            }
            else
            {
                DrawOutlines(c, run);
                ++outlines;
            }
        }
        c.Restore();
        return outlines;
    }
}
