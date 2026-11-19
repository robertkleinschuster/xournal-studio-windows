#include "xournal/pdf/PdfExport.h"

#include "ContentStream.h"
#include "Fonts.h"
#include "Images.h"
#include "PageResources.h"
#include "QpdfSources.h"
#include "TextDrawing.h"
#include "TextLayout.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"
#include "xournal/model/BackgroundRuling.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>

namespace xournal::pdf
{
    namespace
    {
        using namespace detail;

        // COM for WIC on the calling thread (an apartment the caller already has is fine).
        class ComScope
        {
        public:
            ComScope()
            {
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                if (hr == RPC_E_CHANGED_MODE) return; // the thread is in an STA already: usable as it is
                Check("CoInitializeEx", hr);
                initialized = true;
            }
            ~ComScope()
            {
                if (initialized) CoUninitialize();
            }
            ComScope(ComScope const&) = delete;
            ComScope& operator=(ComScope const&) = delete;

        private:
            bool initialized = false;
        };

        class DocumentWriter
        {
        public:
            explicit DocumentWriter(ExportOptions const& options)
                : options(options), foreign(pdf), images(pdf), fonts(pdf), alpha(pdf)
            {
                pdf.emptyPDF();
                pdf.setSuppressWarnings(true); // collected and logged after writing
            }

            std::vector<std::uint8_t> Write(std::span<ExportPage const> pages)
            {
                // Texts first: every glyph of a font must be known before its subset is made.
                for (auto const& page : pages)
                {
                    for (auto const& element : page.elements)
                    {
                        if (auto const* text = std::get_if<XoppText>(&element))
                        {
                            auto runs = layout.Layout(*text);
                            for (auto const& run : runs) fonts.Collect(run);
                            texts.emplace(text, std::move(runs));
                        }
                    }
                }
                fonts.Build();

                for (auto const& page : pages) WritePage(page);

                auto info = QPDFObjectHandle::newDictionary();
                info.replaceKey("/Producer", QPDFObjectHandle::newUnicodeString(options.producer));
                pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));

                QPDFWriter writer(pdf);
                writer.setOutputMemory();
                writer.setDecodeLevel(qpdf_dl_none);         // copied streams keep their data and filters (images unchanged)
                writer.setCompressStreams(true);             // new streams (content, samples, fonts) get Flate
                writer.setObjectStreamMode(qpdf_o_generate);
                writer.setDeterministicID(true);             // no time or randomness in the file
                writer.write();
                LogWarnings(pdf, "export");
                std::unique_ptr<Buffer> buffer(writer.getBuffer());

                result.pages = static_cast<int>(pages.size());
                result.annotationsLeftOut = foreign.AnnotationsLeftOut();
                return { buffer->getBuffer(), buffer->getBuffer() + buffer->getSize() };
            }

            ExportResult const& Result() const { return result; }

        private:
            void WritePage(ExportPage const& page)
            {
                PageResources resources;
                ContentStream c;

                // A PDF background is placed in PDF space (y up), stretched to the page; everything else in page space.
                if (auto const* bg = std::get_if<PdfPageBackground>(&page.background))
                {
                    if (auto form = foreign.PageForm(bg->file, bg->pageIndex))
                    {
                        auto box = ForeignPages::TransformedBox(*form);
                        double sx = page.width / (box.urx - box.llx), sy = page.height / (box.ury - box.lly);
                        c.Save().Transform(sx, 0, 0, sy, -sx * box.llx, -sy * box.lly).Draw(resources.XObject(*form)).Restore();
                    }
                    else
                    {
                        Log(std::format("pdf export: background PDF {} not found; the page stays white", ToUtf8(bg->file.wstring())));
                        ++result.missingBackgrounds;
                    }
                }
                else if (std::holds_alternative<MissingBackground>(page.background))
                {
                    ++result.missingBackgrounds;
                }
                c.Save().Transform(1, 0, 0, -1, 0, page.height); // page space: origin top left, y down
                if (auto const* solid = std::get_if<SolidBackground>(&page.background)) DrawSolid(c, resources, *solid, page);
                if (auto const* picture = std::get_if<PictureBackground>(&page.background)) DrawPictureBackground(c, resources, *picture, page);
                for (auto const& element : page.elements)
                {
                    if (auto const* stroke = std::get_if<StrokeShape>(&element)) DrawStroke(c, resources, *stroke);
                    else if (auto const* picture = std::get_if<Picture>(&element)) DrawPicture(c, resources, *picture);
                    else if (auto const* text = std::get_if<XoppText>(&element))
                        result.textRunsAsOutlines += DrawText(c, resources, alpha, fonts, *text, texts.at(text));
                }
                c.Restore();

                auto pageObject = QPDFObjectHandle::newDictionary();
                pageObject.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
                pageObject.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, page.width, page.height)));
                pageObject.replaceKey("/Resources", resources.Dictionary());
                pageObject.replaceKey("/Contents", pdf.newStream(c.Text()));
                pdf.addPage(pdf.makeIndirectObject(pageObject), false);
            }

            void SetAlpha(ContentStream& c, PageResources& resources, std::uint8_t stroke, std::uint8_t fill)
            {
                if (stroke < 255 || fill < 255) c.GraphicsState(resources.State(alpha.For(stroke, fill)));
            }

            void DrawSolid(ContentStream& c, PageResources& resources, SolidBackground const& bg, ExportPage const& page)
            {
                c.Save();
                SetAlpha(c, resources, 255, bg.paper.a);
                c.FillColor(bg.paper).Rectangle(0, 0, page.width, page.height).FillNonZero().Restore();
                auto const* config = bg.config ? &*bg.config : nullptr;
                for (auto const& ruling : BackgroundRuling::For(bg.style, config, bg.paper, page.width, page.height))
                {
                    c.Save();
                    SetAlpha(c, resources, ruling.color.a, ruling.color.a);
                    if (!ruling.lines.empty())
                    {
                        c.StrokeColor(ruling.color).LineWidth(ruling.width).Cap(StrokeCap::Butt); // as the C# export's pens
                        for (auto const& [from, to] : ruling.lines) c.MoveTo(from).LineTo(to);
                        c.StrokePath();
                    }
                    if (!ruling.dots.empty())
                    {
                        c.FillColor(ruling.color);
                        for (auto const& p : ruling.dots) c.Circle(p, ruling.width / 2);
                        c.FillNonZero();
                    }
                    c.Restore();
                }
            }

            // A picture background that cannot be read leaves its page white (as on screen) and is counted.
            void DrawPictureBackground(ContentStream& c, PageResources& resources, PictureBackground const& bg, ExportPage const& page)
            {
                try
                {
                    auto data = io::Files::ReadAll(bg.file);
                    auto image = images.FromData(data);
                    c.Save().Transform(page.width, 0, 0, -page.height, 0, page.height).Draw(resources.XObject(image)).Restore();
                }
                catch (Error const&)
                {
                    LogException(std::format("pdf export: picture background {}; the page stays white", ToUtf8(bg.file.wstring())));
                    ++result.missingBackgrounds;
                }
            }

            void DrawStroke(ContentStream& c, PageResources& resources, StrokeShape const& s)
            {
                auto parts = s.Parts();
                if (parts.empty()) return;
                // A filled highlighter upstream is one translucent shape with the filling's alpha: filling and line both
                // get that alpha (as the C# export; a transparency group would be exact).
                Color color = s.color;
                if (s.highlighter && s.fill >= 0) color.a = static_cast<std::uint8_t>(std::clamp(s.fill, 0, 255));
                c.Save().RoundJoin().Cap(s.cap);
                bool lineState = false; // the lines' color and alpha are set once, before the first line
                double lineWidth = -1;
                for (auto const& part : parts)
                {
                    if (auto const* fill = std::get_if<StrokeFill>(&part))
                    {
                        Color filling = s.color;
                        filling.a = static_cast<std::uint8_t>(std::clamp(s.fill, 0, 255));
                        c.Save();
                        SetAlpha(c, resources, 255, filling.a);
                        c.FillColor(filling).Polyline(fill->points).ClosePath().FillNonZero().Restore();
                    }
                    else if (auto const* dot = std::get_if<StrokeDot>(&part))
                    {
                        c.Save();
                        SetAlpha(c, resources, 255, color.a);
                        c.FillColor(color);
                        double r = dot->size / 2;
                        if (dot->square) c.Rectangle(dot->center.x - r, dot->center.y - r, dot->size, dot->size);
                        else c.Circle(dot->center, r);
                        c.FillNonZero().Restore();
                    }
                    else if (auto const* line = std::get_if<StrokeLine>(&part))
                    {
                        // Lines come after the filling and are the last parts (StrokeShape::Parts): their state stays set.
                        if (!lineState)
                        {
                            SetAlpha(c, resources, color.a, 255);
                            c.StrokeColor(color);
                            lineState = true;
                        }
                        if (line->width != lineWidth) c.LineWidth(lineWidth = line->width);
                        if (!s.dashes.empty()) c.Dash(s.dashes, line->dashOffset); // page points, as the file stores them
                        c.Polyline(line->points).StrokePath();                     // one path: no darker joints
                    }
                }
                c.Restore();
            }

            void DrawPicture(ContentStream& c, PageResources& resources, Picture const& picture)
            {
                auto const& size = picture.placement.size;
                if (picture.data.empty() || size.width <= 0 || size.height <= 0) return;
                c.Save().Transform(picture.placement.transform);
                if (XoppImage::IsPdf(picture.data))
                {
                    // A copied area stored as a one-page PDF: its page as a form, stretched into the box (y flipped back).
                    auto form = Cached(forms, picture.data, [&] { return foreign.FirstPageOf(picture.data); });
                    auto box = ForeignPages::TransformedBox(form);
                    double sx = size.width / (box.urx - box.llx), sy = size.height / (box.ury - box.lly);
                    c.Transform(sx, 0, 0, -sy, -sx * box.llx, size.height + sy * box.lly).Draw(resources.XObject(form));
                }
                else
                {
                    auto image = Cached(imageObjects, picture.data, [&] { return images.FromData(picture.data); });
                    c.Transform(size.width, 0, 0, -size.height, 0, size.height).Draw(resources.XObject(image));
                }
                c.Restore();
            }

            // The same picture data (duplicated pictures) becomes one object.
            using DataCache = std::unordered_multimap<size_t, std::pair<std::vector<std::uint8_t> const*, QPDFObjectHandle>>;
            template <class Make>
            static QPDFObjectHandle Cached(DataCache& cache, std::vector<std::uint8_t> const& data, Make make)
            {
                size_t hash = std::hash<std::string_view>()(std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
                auto [first, last] = cache.equal_range(hash);
                for (auto it = first; it != last; ++it)
                {
                    if (*it->second.first == data) return it->second.second;
                }
                auto object = make();
                cache.emplace(hash, std::make_pair(&data, object));
                return object;
            }

            ExportOptions const& options;
            QPDF pdf;
            ForeignPages foreign;
            Images images;
            TextLayout layout;
            Fonts fonts;
            AlphaStates alpha;
            std::map<XoppText const*, std::vector<GlyphRun>> texts;
            DataCache imageObjects, forms;
            ExportResult result;
        };

        // qpdf reports damaged or unreadable PDFs with its own exceptions: they become xournal::Error (logged).
        template <class Body>
        auto Guarded(Body body)
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
                std::string message = std::format("pdf export: {}: {}", typeid(e).name(), e.what());
                LogError(message);
                throw Error(message, E_FAIL);
            }
        }
    }

    std::vector<std::uint8_t> ExportToMemory(std::span<ExportPage const> pages, ExportResult& result, ExportOptions const& options)
    {
        return Guarded([&] {
            ComScope com;
            DocumentWriter writer(options);
            auto bytes = writer.Write(pages);
            result = writer.Result();
            return bytes;
        });
    }

    ExportResult Export(std::span<ExportPage const> pages, std::filesystem::path const& target, ExportOptions const& options)
    {
        for (auto const& page : pages)
        {
            if (auto const* bg = std::get_if<PdfPageBackground>(&page.background); bg && Paths::Same(bg->file, target))
            {
                ThrowHr("pdf export: the target is the document's background PDF", HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
            }
        }
        ExportResult result;
        auto bytes = ExportToMemory(pages, result, options);
        io::Files::WriteReplacing(target, bytes); // "<target>~", then moved over the target
        return result;
    }
}
