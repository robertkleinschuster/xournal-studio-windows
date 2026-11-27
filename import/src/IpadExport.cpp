#include "xournal/import/IpadExport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/AppleMarkup.h"
#include "xournal/import/PencilKitDrawing.h"
#include "xournal/io/Files.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <windows.h>

#include <algorithm>
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

        PencilKitStroke Stroke(StrokeShape const& shape)
        {
            PencilKitStroke stroke;
            stroke.ink = "com.apple.ink.pen";
            stroke.color = shape.color;
            stroke.points = shape.points;
            stroke.width = shape.width / PenWidthFactor;
            size_t n = shape.points.size();
            if (!shape.EvenWidth() && shape.segmentWidths.size() + 1 == n)
            {
                for (size_t k = 0; k < n; ++k)
                {
                    double before = k > 0 ? shape.segmentWidths[k - 1] : shape.segmentWidths[0];
                    double after = k + 1 < n ? shape.segmentWidths[k] : shape.segmentWidths[n - 2];
                    stroke.widths.push_back((before + after) / 2 / PenWidthFactor);
                }
            }
            return stroke;
        }

        std::string Num(double v) { return std::format("{:.3f}", v); }

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

        std::string PdfDate()
        {
            SYSTEMTIME t{};
            GetSystemTime(&t);
            return std::format("D:{:04}{:02}{:02}{:02}{:02}{:02}Z00'00'", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        }

        // One page's ink annotation: the strokes' archive and a vector appearance, at the strokes' bounding box.
        void AddInkAnnotation(QPDF& pdf, QPDFObjectHandle page, double width, double height,
                              std::vector<StrokeShape const*> const& strokes)
        {
            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
            std::vector<PencilKitStroke> drawn;
            for (auto const* s : strokes)
            {
                double pad = s->width;
                for (auto const& p : s->points)
                {
                    minX = std::min(minX, p.x - pad); maxX = std::max(maxX, p.x + pad);
                    minY = std::min(minY, p.y - pad); maxY = std::max(maxY, p.y + pad);
                }
                drawn.push_back(Stroke(*s));
            }
            minX = std::max(0.0, minX); minY = std::max(0.0, minY);
            maxX = std::min(width, maxX); maxY = std::min(height, maxY);
            if (maxX <= minX || maxY <= minY) return;
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
            std::string shared = std::format("/BS << /W 0 >> /Border [0 0 0] /DA (/Helvetica 12 Tf 0 g) /F 644 /M {} /Rect {} "
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
        // The editable strokes leave the pages' content: they come back as ink annotations.
        std::vector<std::vector<StrokeShape>> ink(pages.size());
        for (size_t i = 0; i < pages.size(); ++i)
        {
            std::vector<pdf::PageElement> rest;
            for (auto& element : pages[i].elements)
            {
                auto* stroke = std::get_if<StrokeShape>(&element);
                if (stroke && IsEditable(*stroke))
                {
                    ink[i].push_back(std::move(*stroke));
                    continue;
                }
                if (stroke) ++result.flatStrokes;
                rest.push_back(std::move(element));
            }
            pages[i].elements = std::move(rest);
        }
        auto bytes = pdf::ExportToMemory(pages, result.pdf, options);
        result.pdf.skippedElements = counts.skippedElements;

        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("iPad export", reinterpret_cast<char const*>(bytes.data()), bytes.size());
        auto qpages = QPDFPageDocumentHelper(pdf).getAllPages();
        for (size_t i = 0; i < qpages.size() && i < ink.size(); ++i)
        {
            if (ink[i].empty()) continue;
            std::vector<StrokeShape const*> strokes;
            for (auto const& s : ink[i]) strokes.push_back(&s);
            AddInkAnnotation(pdf, qpages[i].getObjectHandle(), pages[i].width, pages[i].height, strokes);
            ++result.drawings;
            result.strokes += static_cast<int>(strokes.size());
        }
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setDeterministicID(true);
        writer.write();
        for (auto const& w : pdf.getWarnings()) Log(std::format("iPad export: qpdf warning: {}", w.what()));
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        io::Files::WriteReplacing(target, std::span<std::uint8_t const>(buffer->getBuffer(), buffer->getSize()));
        return result;
    }
}
