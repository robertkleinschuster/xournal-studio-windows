#include "xournal/import/PdfMarkupImport.h"

#include "AppearanceText.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/AppleMarkup.h"
#include "xournal/import/PencilKitDrawing.h"
#include "xournal/io/Files.h"
#include "xournal/model/Color.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"
#include "xournal/pdf/PageFrame.h"
#include "xournal/pdf/PdfAreaRenderer.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <typeinfo>

namespace xournal::import
{
    namespace
    {
        constexpr std::uint8_t HighlighterAlpha = 0x7f; // as Xournal stores highlighters
        constexpr int Hidden = 1 << 1, NoView = 1 << 5;

        std::wstring Format(double v) { return XoppFormat::FormatNumber(v, 3); }

        using pdf::PageFrame;
        using pdf::PdfBox;

        std::optional<PdfBox> BoxOf(QPDFObjectHandle o)
        {
            if (!o.isRectangle()) return std::nullopt;
            auto r = o.getArrayAsRectangle();
            return PdfBox{ r.llx, r.lly, r.urx, r.ury };
        }

        /// The page as Windows.Data.Pdf shows it (crop box, else media box, turned by /Rotate): pdf::PageFrame.
        PageFrame FrameOf(QPDFPageObjectHelper& page)
        {
            auto media = BoxOf(page.getAttribute("/MediaBox", false));
            if (!media) ThrowInvalidData("import: a page without media box");
            auto rotation = page.getAttribute("/Rotate", false);
            return PageFrame::Of(BoxOf(page.getAttribute("/CropBox", false)), *media, rotation.isInteger() ? rotation.getIntValue() : 0);
        }

        std::string Subtype(QPDFObjectHandle a)
        {
            auto s = a.getKey("/Subtype");
            return s.isName() ? s.getName() : std::string();
        }

        PdfBox RectOf(QPDFObjectHandle a)
        {
            return BoxOf(a.getKey("/Rect")).value_or(PdfBox{});
        }

        std::optional<AppleMarkup> Markup(QPDFObjectHandle annotation)
        {
            auto extras = annotation.getKey("/AAPL:AKExtras");
            if (!extras.isDictionary()) return std::nullopt;
            for (char const* key : { "/AAPL:AKAnnotationV2", "/AAPL:AKAnnotationObject" })
            {
                auto archive = extras.getKey(key);
                if (!archive.isString() || archive.getStringValue().empty()) continue;
                try
                {
                    return AppleMarkup::Read(archive.getStringValue());
                }
                catch (Error const&)
                {
                    LogException(std::format("import: {}", key));
                }
            }
            return std::nullopt;
        }

        bool IsInkDrawing(std::optional<AppleMarkup> const& m) { return m && m->className == AppleMarkup::InkClass && m->drawing; }

        // Shown by a viewer: not hidden and an appearance with an area (Apple writes empty ones for emptied drawings).
        bool Shows(QPDFObjectHandle annotation)
        {
            auto flags = annotation.getKey("/F");
            if (flags.isInteger() && (flags.getIntValue() & (Hidden | NoView)) != 0) return false;
            auto normal = annotation.getKey("/AP").getKey("/N");
            auto dict = normal.isStream() ? normal.getDict() : normal;
            if (!dict.isDictionary() || !dict.getKey("/BBox").isRectangle() || !annotation.getKey("/Rect").isRectangle()) return false;
            auto box = dict.getKey("/BBox").getArrayAsRectangle();
            auto rect = RectOf(annotation);
            return box.urx != box.llx && box.ury != box.lly && rect.urx != rect.llx && rect.ury != rect.lly &&
                   std::isfinite(rect.llx) && std::abs(rect.llx) < 1e6 && std::abs(rect.lly) < 1e6;
        }

        bool IsMarkup(QPDFObjectHandle a)
        {
            auto s = Subtype(a);
            return s != "/Link" && s != "/Widget" && s != "/Popup";
        }

        // iPadOS (PaperKit) leaves a drawing's earlier appearance as a stamp of its own beside the drawing: no markup
        // archive, only PaperKit ids (/PPKID), the drawing's exact rectangle; its picture is the drawing's (measured on the
        // user's PDFs: pixel for pixel the same). Taken with the drawing, it would lie under its strokes as a stale picture.
        bool IsOlderCopyOfDrawing(QPDFObjectHandle annotation, std::vector<PdfBox> const& drawings)
        {
            auto extras = annotation.getKey("/AAPL:AKExtras");
            if (Subtype(annotation) != "/Stamp" || !extras.isDictionary() || extras.hasKey("/AAPL:AKAnnotationV2") ||
                extras.hasKey("/AAPL:AKAnnotationObject") || !extras.hasKey("/PPKID"))
            {
                return false;
            }
            auto r = RectOf(annotation);
            return std::any_of(drawings.begin(), drawings.end(), [&](PdfBox const& d) {
                return std::abs(d.llx - r.llx) < 0.5 && std::abs(d.lly - r.lly) < 0.5 && std::abs(d.urx - r.urx) < 0.5 &&
                       std::abs(d.ury - r.ury) < 0.5;
            });
        }

        // A vertex of a stroke with its drawn width.
        struct Vertex
        {
            Point p;
            double width = 0;
        };

        // Douglas-Peucker on position and width together: a point goes when it lies within `positionTolerance` of the line
        // between its neighbours' kept points and has the width of the segment's start (a segment has one width, as in
        // Xournal and as the export writes it) within `widthTolerance` or 10 %. The dense control points PencilKit stores (the
        // export writes one every 0.8 pt) come back as the corners they were. Idempotent: the points kept were the farthest
        // ones, so a second pass keeps them again.
        void Simplify(std::vector<Vertex> const& v, size_t first, size_t last, double positionTolerance, double widthTolerance,
                      std::vector<bool>& keep)
        {
            if (last <= first + 1) return;
            auto const& a = v[first];
            auto const& b = v[last];
            double dx = b.p.x - a.p.x, dy = b.p.y - a.p.y, length2 = dx * dx + dy * dy;
            double worst = 0;
            size_t at = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                double t = length2 > 0 ? std::clamp(((v[i].p.x - a.p.x) * dx + (v[i].p.y - a.p.y) * dy) / length2, 0.0, 1.0) : 0;
                double distance = std::hypot(v[i].p.x - (a.p.x + t * dx), v[i].p.y - (a.p.y + t * dy));
                double widthError = std::abs(v[i].width - a.width) / std::max(widthTolerance, 0.1 * a.width);
                double error = std::max(distance / positionTolerance, widthError);
                if (error > worst)
                {
                    worst = error;
                    at = i;
                }
            }
            if (worst <= 1) return;
            keep[at] = true;
            Simplify(v, first, at, positionTolerance, widthTolerance, keep);
            Simplify(v, at, last, positionTolerance, widthTolerance, keep);
        }

        XmlElementPtr StrokeElement(PencilKitStroke const& stroke, PageFrame const& frame, AppleMarkup const& markup)
        {
            // Drawing coordinates span the page (drawingSize = its unrotated size), top left, y down.
            double sx = markup.drawingWidth > 0 ? frame.width / markup.drawingWidth : 1;
            double sy = markup.drawingHeight > 0 ? frame.height / markup.drawingHeight : 1;
            double scale = std::sqrt(sx * sy);
            bool marker = stroke.IsMarker();
            bool perPoint = !marker && stroke.widths.size() == stroke.points.size();
            double constant = marker ? stroke.width * scale * MarkerWidthFactor : PenDrawnWidth(stroke.width * scale);
            std::vector<Vertex> vertices;
            for (size_t i = 0; i < stroke.points.size(); ++i)
            {
                auto const& p = stroke.points[i];
                double width = perPoint && stroke.widths[i] > 0 ? PenDrawnWidth(stroke.widths[i] * scale) : constant;
                vertices.push_back({ frame.FromTopLeft(p.x * sx, p.y * sy), width });
            }
            if (vertices.empty()) return nullptr;
            // The iPad and the export write a point as a stroke of a fraction of a point: a dot again (two equal points).
            double extent = 0;
            for (size_t i = 1; i < vertices.size(); ++i) extent += std::hypot(vertices[i].p.x - vertices[i - 1].p.x, vertices[i].p.y - vertices[i - 1].p.y);
            if (extent < 0.5)
            {
                Vertex middle = vertices[vertices.size() / 2];
                vertices = { middle, middle }; // Xournal's strokes have two points at least
            }
            else if (vertices.size() > 2)
            {
                std::vector<bool> keep(vertices.size(), false);
                keep.front() = keep.back() = true;
                Simplify(vertices, 0, vertices.size() - 1, 0.02, 0.03, keep);
                std::vector<Vertex> kept;
                for (size_t i = 0; i < vertices.size(); ++i)
                    if (keep[i]) kept.push_back(vertices[i]);
                vertices = std::move(kept);
            }
            Color color = stroke.color;
            if (marker) color.a = HighlighterAlpha;
            std::wstring text;
            for (auto const& v : vertices)
            {
                if (!text.empty()) text += L' ';
                text += Format(v.p.x) + L' ' + Format(v.p.y);
            }
            // One width for the stroke, or - when the pen's pressure changed it - the stroke's width followed by that of each
            // segment (a segment has the width of the point it starts at, as the export writes them).
            double lowest = vertices[0].width, highest = vertices[0].width;
            std::vector<double> sorted;
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                lowest = std::min(lowest, vertices[i].width);
                highest = std::max(highest, vertices[i].width);
                if (i + 1 < vertices.size()) sorted.push_back(vertices[i].width);
            }
            std::sort(sorted.begin(), sorted.end());
            double nominal = std::max(sorted[sorted.size() / 2], 0.1);
            std::wstring widths = Format(nominal);
            if (highest - lowest > 0.1)
            {
                for (size_t i = 0; i + 1 < vertices.size(); ++i) widths += L' ' + Format(std::max(vertices[i].width, 0.1));
            }
            auto e = XmlElement::Make(L"stroke", { { L"tool", marker ? L"highlighter" : L"pen" },
                                                   { L"color", FormatColor(color) },
                                                   { L"width", widths } });
            e->SetValue(std::move(text));
            return e;
        }

        // A text box: its text where and as large as its appearance draws it (the first line's start; Apple drew 11 pt
        // for a /DA of 12 pt), else at its rectangle's top left in the /DA's size; the color of its /DA. A .xopp text's
        // y is the top of its first line: the baseline minus the ascent of the font the app shows (Arial, 0.905 em).
        XmlElementPtr TextElement(QPDFObjectHandle annotation, PageFrame const& frame)
        {
            auto contents = annotation.getKey("/Contents");
            std::wstring content = contents.isString() ? FromUtf8(contents.getUTF8Value()) : std::wstring();
            if (Trim(content).empty() || frame.rotate != 0) return nullptr; // nothing to write, or rotated: a picture
            auto da = annotation.getKey("/DA");
            auto appearance = detail::ParseDefaultAppearance(da.isString() ? da.getStringValue() : std::string());
            double size = appearance.size;
            auto area = frame.Area(RectOf(annotation));
            double x = area.x + 2, y = area.y + 2;
            if (auto line = detail::FirstLineOf(annotation))
            {
                constexpr double Ascent = 0.905;
                size = line->size;
                auto start = frame.FromUserSpace(line->x, line->y);
                x = start.x;
                y = start.y - Ascent * size;
            }
            std::wstring text;
            for (size_t i = 0; i < content.size(); ++i)
            {
                if (content[i] == L'\r')
                {
                    text += L'\n';
                    if (i + 1 < content.size() && content[i + 1] == L'\n') ++i;
                }
                else text += content[i];
            }
            auto e = XmlElement::Make(L"text", { { L"font", L"Sans" }, { L"size", Format(size) }, { L"x", Format(x) },
                                                 { L"y", Format(y) }, { L"color", FormatColor(appearance.color) } });
            e->SetValue(std::move(text));
            return e;
        }

        enum class Taken { Kept, Removed, Strokes, Text };

        Taken Take(QPDFObjectHandle annotation, std::optional<AppleMarkup> const& apple, PageFrame const& frame,
                   std::vector<XmlElementPtr>& added, bool& picture)
        {
            picture = false;
            if (!IsMarkup(annotation)) return Taken::Kept;
            if (IsInkDrawing(apple))
            {
                try
                {
                    auto strokes = PencilKitDrawing::Read(*apple->drawing);
                    for (auto const& s : strokes)
                    {
                        if (auto e = StrokeElement(s, frame, *apple)) added.push_back(e);
                    }
                    if (!strokes.empty()) return Taken::Strokes;
                    if (!Shows(annotation)) return Taken::Removed;
                    picture = true;
                    return Taken::Removed;
                }
                catch (Error const&)
                {
                    LogException("import: drawing of an ink annotation; taken as a picture");
                }
            }
            if (Subtype(annotation) == "/FreeText")
            {
                if (auto text = TextElement(annotation, frame))
                {
                    added.push_back(text);
                    return Taken::Text;
                }
            }
            if (!Shows(annotation)) return Taken::Kept;
            picture = true;
            return Taken::Removed;
        }

        bool Overlap(Rect const& a, Rect const& b)
        {
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
        }

        void SetAnnotations(QPDFObjectHandle page, std::vector<QPDFObjectHandle> const& keep)
        {
            auto annotations = page.getKey("/Annots");
            if (!annotations.isArray() || annotations.getArrayNItems() == static_cast<int>(keep.size())) return;
            if (keep.empty())
            {
                page.removeKey("/Annots");
                return;
            }
            auto array = QPDFObjectHandle::newArray();
            for (auto const& item : keep) array.appendItem(item);
            page.replaceKey("/Annots", array);
        }

        std::shared_ptr<QPDF> Open(std::filesystem::path const& file)
        {
            auto pdf = std::make_shared<QPDF>();
            pdf->setSuppressWarnings(true); // collected and logged
            std::string utf8 = ToUtf8(Paths::Full(file).wstring());
            pdf->processFile(utf8.c_str());
            return pdf;
        }

        void Write(QPDF& pdf, std::filesystem::path const& target)
        {
            QPDFWriter writer(pdf);
            writer.setOutputMemory();
            writer.write();
            for (auto const& w : pdf.getWarnings()) Log(std::format("import: qpdf warning ({}): {}", ToUtf8(target.wstring()), w.what()));
            std::unique_ptr<Buffer> buffer(writer.getBuffer());
            io::Files::WriteReplacing(target, std::span<std::uint8_t const>(buffer->getBuffer(), buffer->getSize()));
        }

        // The source's pages with nothing but the annotations taken as pictures: Windows.Data.Pdf renders them alone.
        void WritePicturesPdf(std::filesystem::path const& source, std::vector<std::set<int>> const& positions,
                              std::filesystem::path const& target)
        {
            auto pdf = Open(source);
            auto pages = QPDFPageDocumentHelper(*pdf).getAllPages();
            for (size_t index = 0; index < pages.size(); ++index)
            {
                auto page = pages[index].getObjectHandle();
                page.replaceKey("/Contents", pdf->newStream(std::string()));
                std::vector<QPDFObjectHandle> keep;
                auto annotations = page.getKey("/Annots");
                if (index < positions.size() && annotations.isArray())
                {
                    // The same annotations in this second reading of the file: by their position in /Annots.
                    for (int i = 0; i < annotations.getArrayNItems(); ++i)
                    {
                        if (positions[index].contains(i)) keep.push_back(annotations.getArrayItem(i));
                    }
                }
                if (annotations.isArray()) SetAnnotations(page, keep);
            }
            Write(*pdf, target);
        }

        template <class Body>
        auto Guarded(char const* what, Body body)
        {
            try
            {
                return body();
            }
            catch (Error const&)
            {
                throw;
            }
            catch (std::exception const& e)
            {
                // qpdf reports damaged PDFs with its own exceptions.
                std::string message = std::format("{}: {}: {}", what, typeid(e).name(), e.what());
                LogError(message);
                throw Error(message, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            }
        }
    }

    MarkupImport Prepare(std::filesystem::path const& source, std::filesystem::path const& targetPdf,
                         std::function<std::filesystem::path(int)> const& picturesPdf)
    {
        return Guarded("import", [&] {
            auto document = Open(source);
            MarkupImport import;
            std::vector<std::vector<std::set<int>>> rounds; // per pictures PDF, per page: positions in /Annots
            auto pages = QPDFPageDocumentHelper(*document).getAllPages();
            for (size_t index = 0; index < pages.size(); ++index)
            {
                auto frame = FrameOf(pages[index]);
                import.pageSizes.push_back(frame.Shown());
                auto page = pages[index].getObjectHandle();
                std::vector<XmlElementPtr> added;
                std::vector<std::pair<Rect, int>> pictureAreas;
                std::vector<QPDFObjectHandle> keep;
                std::vector<QPDFObjectHandle> items;
                if (auto annotations = page.getKey("/Annots"); annotations.isArray()) items = annotations.getArrayAsVector();
                // Apple's archive of each annotation, read once.
                std::vector<std::optional<AppleMarkup>> markup;
                std::vector<PdfBox> drawingAreas;
                for (auto const& item : items)
                {
                    markup.push_back(item.isDictionary() ? Markup(item) : std::nullopt);
                    if (IsInkDrawing(markup.back())) drawingAreas.push_back(RectOf(item));
                }
                for (size_t position = 0; position < items.size(); ++position)
                {
                    auto const& annotation = items[position];
                    if (!annotation.isDictionary())
                    {
                        keep.push_back(annotation);
                        continue;
                    }
                    if (IsOlderCopyOfDrawing(annotation, drawingAreas)) continue; // removed: the drawing comes with its strokes
                    bool picture = false;
                    switch (Take(annotation, markup[position], frame, added, picture))
                    {
                    case Taken::Strokes: ++import.drawings; break;
                    case Taken::Text: ++import.texts; break;
                    case Taken::Removed: break;
                    case Taken::Kept:
                        keep.push_back(annotation);
                        if (IsMarkup(annotation)) ++import.kept;
                        break;
                    }
                    if (picture)
                    {
                        auto area = frame.Area(RectOf(annotation));
                        // The first pictures PDF where it overlaps no other picture of this page.
                        int round = 0;
                        while (std::any_of(pictureAreas.begin(), pictureAreas.end(),
                                           [&](auto const& p) { return p.second == round && Overlap(p.first, area); }))
                        {
                            ++round;
                        }
                        pictureAreas.emplace_back(area, round);
                        if (rounds.size() <= static_cast<size_t>(round)) rounds.resize(static_cast<size_t>(round) + 1);
                        if (rounds[static_cast<size_t>(round)].size() <= index) rounds[static_cast<size_t>(round)].resize(index + 1);
                        rounds[static_cast<size_t>(round)][index].insert(static_cast<int>(position));
                        auto image = XmlElement::Make(L"image");
                        added.push_back(image);
                        import.pictures.push_back({ picturesPdf(round), static_cast<int>(index), area, image });
                    }
                }
                SetAnnotations(page, keep);
                import.elements.push_back(std::move(added));
            }
            Write(*document, targetPdf);
            for (size_t round = 0; round < rounds.size(); ++round)
            {
                WritePicturesPdf(source, rounds[round], picturesPdf(static_cast<int>(round)));
            }
            return import;
        });
    }

    void RenderPictures(MarkupImport& import, double pixelsPerPoint)
    {
        std::map<std::filesystem::path, std::vector<MarkupPicture*>> byPdf;
        for (auto& picture : import.pictures) byPdf[picture.pdf].push_back(&picture);
        for (auto& [pdf, pictures] : byPdf)
        {
            pdf::PdfAreaRenderer renderer(pdf); // its document goes after this group
            for (auto* picture : pictures)
            {
                auto rendered = renderer.Render(picture->page, picture->area, pixelsPerPoint);
                auto image = XoppImage::Create(picture->area, rendered.png, rendered.width, rendered.height);
                for (auto const& attribute : image->Attributes()) picture->element->SetAttribute(attribute.name, attribute.value);
                picture->element->SetValue(image->Value());
            }
        }
    }

    std::unique_ptr<XoppDocument> CreateDocument(MarkupImport const& import, std::filesystem::path const& pdf)
    {
        auto doc = XoppDocument::FromPdf(pdf, import.pageSizes);
        for (size_t i = 0; i < doc->Pages().size() && i < import.elements.size(); ++i)
        {
            std::vector<XmlElementPtr> elements;
            for (auto const& e : import.elements[i])
            {
                if (e->Name() != L"image" || !e->Value().empty()) elements.push_back(e);
            }
            doc->Pages()[i]->AddToTopLayer(elements);
        }
        return doc;
    }
}
