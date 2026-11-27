#include "xournal/import/IpadExport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/AppleMarkup.h"
#include "xournal/import/PencilKitDrawing.h"
#include "xournal/io/Files.h"

#include "IpadElements.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>

namespace xournal::import
{
    namespace
    {
        // What the markup can hold: opaque round-ended pen lines. Highlighters (PencilKit's marker ink is another kind of
        // ink whose layout is not known from the user's PDFs), dashed, filled and square or butt ended strokes stay drawn.
        bool IsEditable(StrokeShape const& s)
        {
            return !s.highlighter && s.cap == StrokeCap::Round && s.dashes.empty() && s.fill < 0 && s.color.a == 0xFF &&
                   !s.points.empty();
        }

        // PencilKit stores the control points of a smooth curve (uniform cubic B-spline) - every point of the user's own
        // drawings lies about 0.8 pt from the next (never more than 2.1 pt) - while Xournal stores a polyline whose
        // corners and straight lines have two or a few points. The points are written so dense that the curve through
        // them is the polyline itself (corners rounded by a fraction of a point), widths interpolated.
        constexpr double PointSpacing = 0.8;

        PencilKitStroke Stroke(StrokeShape const& shape)
        {
            PencilKitStroke stroke;
            stroke.ink = "com.apple.ink.pen";
            stroke.color = shape.color;
            stroke.width = PenBrushSize(shape.width);
            size_t n = shape.points.size();
            std::vector<double> sizes(n, stroke.width); // each original point's brush size
            if (!shape.EvenWidth() && shape.segmentWidths.size() + 1 == n)
            {
                // A point's width is that of the segment that starts there (the last point: of the last segment), the
                // import's reading of it - so a round trip returns each segment's own width.
                for (size_t k = 0; k < n; ++k) sizes[k] = PenBrushSize(shape.segmentWidths[std::min(k, n - 2)]);
            }
            double length = 0;
            for (size_t k = 0; k + 1 < n; ++k) length += std::hypot(shape.points[k + 1].x - shape.points[k].x, shape.points[k + 1].y - shape.points[k].y);
            if (length < 0.1) // one point, or a tap (Xournal++ stores two equal points)
            {
                // A dot: a stroke of a fraction of a point, round ends make it the circle.
                for (int k = -8; k <= 8; ++k)
                {
                    stroke.points.push_back({ shape.points[0].x + 0.02 * k, shape.points[0].y });
                    stroke.widths.push_back(sizes[0]);
                }
                return stroke;
            }
            for (size_t k = 0; k + 1 < n; ++k)
            {
                Point a = shape.points[k], b = shape.points[k + 1];
                int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / PointSpacing)));
                for (int i = 0; i < pieces; ++i)
                {
                    double f = static_cast<double>(i) / pieces;
                    stroke.points.push_back({ a.x + f * (b.x - a.x), a.y + f * (b.y - a.y) });
                    stroke.widths.push_back(sizes[k]); // the whole segment has its width, as in Xournal (no interpolated, rounded values)
                }
            }
            stroke.points.push_back(shape.points[n - 1]);
            stroke.widths.push_back(sizes[n - 1]);
            return stroke;
        }

        using detail::Num;
        using detail::PdfDate;

        // What goes where: the pen strokes the PencilKit drawing can hold (per page) go to `ink`; the other strokes (highlighters,
        // dashed, filled, square or butt ended, transparent, off the page) and the texts become standard annotations
        // (`standard`, in the page's order, with the .xopp element they come from); pictures and what the app does not read are
        // counted in `omitted`. Nothing is drawn into the page: the pages' elements are removed.
        struct Standard
        {
            pdf::PageElement element;
            XmlElementPtr origin;
        };

        void Partition(std::vector<pdf::ExportPage>& pages, std::vector<std::vector<StrokeShape>>& ink,
                       std::vector<std::vector<Standard>>& standard, IpadOmitted& omitted)
        {
            ink.assign(pages.size(), {});
            standard.assign(pages.size(), {});
            for (size_t i = 0; i < pages.size(); ++i)
            {
                for (size_t e = 0; e < pages[i].elements.size(); ++e)
                {
                    auto& element = pages[i].elements[e];
                    XmlElementPtr origin = e < pages[i].origin.size() ? pages[i].origin[e] : nullptr;
                    if (auto* stroke = std::get_if<StrokeShape>(&element))
                    {
                        // A pen stroke is a drawing's when the markup can hold it and its whole-number bounds lie on the page.
                        bool editable = IsEditable(*stroke);
                        if (editable)
                        {
                            Rect bounds = PencilKitDrawing::BoundsOf(Stroke(*stroke));
                            editable = bounds.x >= 0 && bounds.y >= 0 && bounds.x + bounds.width <= pages[i].width &&
                                       bounds.y + bounds.height <= pages[i].height;
                        }
                        if (editable) ink[i].push_back(std::move(*stroke));
                        else standard[i].push_back({ std::move(element), origin });
                    }
                    else if (std::holds_alternative<XoppText>(element)) standard[i].push_back({ std::move(element), origin });
                    else ++omitted.pictures;
                }
                pages[i].elements.clear();
                pages[i].origin.clear();
            }
        }

        // The strokes as the annotation's appearance shows them: round-ended lines, one per segment when the width
        // changes. `ox`, `oy`: the box's top left on the page; the form's origin is its lower left.
        std::string AppearanceContent(std::vector<StrokeShape const*> const& strokes, double ox, double oy, double boxHeight)
        {
            std::string c = "q 1 J 1 j\n";
            auto at = [&](Point p) { return Num(p.x - ox) + " " + Num(boxHeight - (p.y - oy)); };
            for (auto const* s : strokes)
            {
                c += std::format("{} {} {} RG\n", Num(s->color.r / 255.0), Num(s->color.g / 255.0), Num(s->color.b / 255.0));
                if (s->points.size() == 1 || s->EvenWidth())
                {
                    c += Num(s->width) + " w\n" + at(s->points[0]) + " m\n";
                    if (s->points.size() == 1) c += at(s->points[0]) + " l\n";
                    for (size_t k = 1; k < s->points.size(); ++k) c += at(s->points[k]) + " l\n";
                    c += "S\n";
                    continue;
                }
                for (size_t k = 1; k < s->points.size(); ++k)
                {
                    double w = k - 1 < s->segmentWidths.size() ? s->segmentWidths[k - 1] : s->width;
                    if (w <= 0) continue;
                    c += Num(w) + " w " + at(s->points[k - 1]) + " m " + at(s->points[k]) + " l S\n";
                }
            }
            return c + "Q\n";
        }

        std::string NewUuid()
        {
            std::mt19937_64 random{ std::random_device{}() };
            std::uint64_t a = random(), b = random();
            a = (a & ~0xF000ull) | 0x4000ull;             // version 4
            b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
            return std::format("{:08X}-{:04X}-{:04X}-{:04X}-{:012X}", a >> 32, (a >> 16) & 0xFFFF, a & 0xFFFF, b >> 48, b & 0xFFFFFFFFFFFFull);
        }

        // One page's ink annotation: the strokes' archive and a vector appearance. As on iPadOS (every file of the user's)
        // the rectangle is the union of the strokes' bounds (whole numbers), the same in the archive, /Rect and /BBox; the
        // drawing's own coordinates stay those of the page.
        void AddInkAnnotation(QPDF& pdf, QPDFObjectHandle page, double width, double height,
                              std::vector<StrokeShape const*> const& strokes)
        {
            std::vector<PencilKitStroke> drawn;
            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            for (auto const* s : strokes)
            {
                drawn.push_back(Stroke(*s));
                Rect b = PencilKitDrawing::BoundsOf(drawn.back());
                minX = std::min(minX, b.x); maxX = std::max(maxX, b.x + b.width);
                minY = std::min(minY, b.y); maxY = std::max(maxY, b.y + b.height);
            }
            double boxWidth = maxX - minX, boxHeight = maxY - minY;
            double lly = height - maxY; // PDF user space: y up
            auto drawing = PencilKitDrawing::WriteLegacy(drawn);
            std::string uuid = NewUuid();
            std::string archive = AppleMarkup::WriteInk(drawing, width, height, minX, lly, boxWidth, boxHeight, uuid);

            auto appearance = pdf.newStream(AppearanceContent(strokes, minX, minY, boxHeight));
            appearance.replaceDict(QPDFObjectHandle::parse(&pdf, std::format(
                "<< /Type /XObject /Subtype /Form /FormType 1 /BBox [0 0 {} {}] /Resources << /ProcSet [/PDF] >> >>",
                Num(boxWidth), Num(boxHeight))));
            std::string rect = std::format("[{} {} {} {}]", Num(minX), Num(lly), Num(maxX), Num(height - minY));
            std::string date = std::format("({})", PdfDate());
            std::string shared = std::format("/BS << /W 0 >> /Border [0 0 0] /DA (/Helvetica 12 Tf 0 g) /F 644 /Ff 1 /M {} /Rect {} "
                                             "/Subtype /Stamp /T (Xournal Studio) /Type /Annot", date, rect);
            auto annotation = QPDFObjectHandle::parse(&pdf, std::format(
                "<< {} /AAPL:AKExtras << /AAPL:AKAnnotationV2 ({}) /AAPL:AKPDFAnnotationDictionary << {} >> >> >>", shared,
                archive, shared));
            annotation.replaceKey("/AP", QPDFObjectHandle::newDictionary({ { "/N", pdf.makeIndirectObject(appearance) } }));
            auto annots = page.getKey("/Annots");
            if (!annots.isArray())
            {
                annots = QPDFObjectHandle::newArray();
                page.replaceKey("/Annots", annots);
            }
            annots.appendItem(pdf.makeIndirectObject(annotation));
        }
    }

    IpadExportResult ExportForIpad(pdf::DocumentExportSource const& source, std::filesystem::path const& target,
                                   pdf::ExportOptions const& options)
    {
        if (source.backgroundPdf && Paths::Same(*source.backgroundPdf, target))
        {
            ThrowHr("iPad export: the target is the document's background PDF", HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
        }
        IpadExportResult result;
        pdf::ExportResult counts;
        auto pages = pdf::PagesOf(source, counts);
        // The editable strokes become ink annotations; everything else is left out and counted.
        std::vector<std::vector<StrokeShape>> ink;
        std::vector<std::vector<Standard>> standard;
        Partition(pages, ink, standard, result.omitted);
        result.omitted.other = counts.skippedElements;
        auto bytes = pdf::ExportToMemory(pages, result.pdf, options);
        result.pdf.skippedElements = 0;

        detail::ForeignKeep keep; // the appearances' documents live until the file is written
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("iPad export", reinterpret_cast<char const*>(bytes.data()), bytes.size());
        auto qpages = QPDFPageDocumentHelper(pdf).getAllPages();
        for (size_t i = 0; i < qpages.size() && i < ink.size(); ++i)
        {
            // Standard annotations first (below), the pen drawing over them.
            for (auto const& item : standard[i])
            {
                detail::AddElementAnnotation(pdf, keep, qpages[i].getObjectHandle(), pages[i].height, item.element, item.origin, options);
                ++result.elements;
            }
            if (ink[i].empty()) continue;
            std::vector<StrokeShape const*> strokes;
            for (auto const& s : ink[i]) strokes.push_back(&s);
            AddInkAnnotation(pdf, qpages[i].getObjectHandle(), pages[i].width, pages[i].height, strokes);
            ++result.drawings;
            result.strokes += static_cast<int>(strokes.size());
        }
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setDecodeLevel(qpdf_dl_none);         // streams keep their data and filters (the pages are kept exactly)
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setDeterministicID(true);
        writer.write();
        for (auto const& w : pdf.getWarnings()) Log(std::format("iPad export: qpdf warning: {}", w.what()));
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        io::Files::WriteReplacing(target, std::span<std::uint8_t const>(buffer->getBuffer(), buffer->getSize()));
        return result;
    }

    IpadOmitted OmittedByIpadExport(pdf::DocumentExportSource const& source)
    {
        pdf::ExportResult counts;
        auto pages = pdf::PagesOf(source, counts);
        std::vector<std::vector<StrokeShape>> ink;
        std::vector<std::vector<Standard>> standard;
        IpadOmitted omitted;
        Partition(pages, ink, standard, omitted);
        omitted.other = counts.skippedElements;
        return omitted;
    }
}
