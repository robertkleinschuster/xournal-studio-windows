// The iPad import (port of XournalStudio.Tests/MarkupImportTests.cs): PencilKit drawings (both variants), Apple's
// markup archive (binary plist) and the import of a PDF built here the way iPadOS writes its markup. Synthetic inputs
// only: protobuf written with protozero, binary plists with the small writer below, PDFs with qpdf.

#include "../PdfTestSupport.h"
#include "../TestSupport.h"

#include "../../import/src/BinaryPlist.h"
#include "../../import/src/BinaryPlistWriter.h"



#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/AppleMarkup.h"
#include "xournal/import/IpadExport.h"
#include "xournal/import/PdfMarkupImport.h"
#include "xournal/import/PencilKitDrawing.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/io/Gzip.h"
#include "xournal/model/Snapshot.h"
#include "xournal/pdf/DocumentExport.h"
#include "xournal/pdf/PdfAreaRenderer.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"

#include <protozero/pbf_reader.hpp>
#include <protozero/pbf_writer.hpp>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <gtest/gtest.h>
#include <windows.h>
#include <wincrypt.h>

#include <bit>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <variant>

namespace xournal::tests
{
    namespace
    {
        using namespace xournal::import;

        // ------------------------------------------------------------------ protobuf builders (protozero)

        using Writer = std::function<void(protozero::pbf_writer&)>;

        std::string Message(Writer const& write)
        {
            std::string buffer;
            {
                protozero::pbf_writer w(buffer);
                write(w);
            }
            return buffer;
        }

        template <class T>
        void Append(std::string& s, T value)
        {
            char bytes[sizeof(T)];
            std::memcpy(bytes, &value, sizeof(T));
            s.append(bytes, sizeof(T));
        }

        std::string Ink(std::string const& id, float r, float g, float b, float a)
        {
            return Message([&](protozero::pbf_writer& o) {
                o.add_bytes(1, Message([&](protozero::pbf_writer& c) { c.add_float(1, r); c.add_float(2, g); c.add_float(3, b); c.add_float(4, a); }));
                o.add_bytes(2, id);
            });
        }

        // A path with location (bit 0) and width (bit 2) per point.
        std::string Path(std::vector<std::pair<float, float>> const& points, float width)
        {
            return Message([&](protozero::pbf_writer& o) {
                o.add_uint64(3, points.size());
                o.add_uint64(4, 0b101);
                o.add_uint64(5, 0xFFF & ~0b101ull);
                // The other channels, constant: time, aspect 1, edge, force 1, azimuth, altitude, opacity 1, radius,
                // threshold, elevation.
                std::string constants;
                Append(constants, 0.0f);
                for (std::uint16_t v : std::initializer_list<std::uint16_t>{ 1000, 0, 1000, 0, 0, 32768 }) Append(constants, v);
                Append(constants, 0.0f);
                Append(constants, std::uint16_t{ 0 });
                Append(constants, 0.0f);
                o.add_bytes(6, constants);
                std::string records;
                for (auto [x, y] : points)
                {
                    Append(records, x);
                    Append(records, y);
                    Append(records, width);
                }
                o.add_bytes(7, records);
            });
        }

        std::vector<std::uint8_t> Wrd(std::vector<std::string> const& inks, std::vector<std::string> const& strokes)
        {
            std::string body = Message([&](protozero::pbf_writer& o) {
                for (auto const& ink : inks) o.add_bytes(4, ink);
                for (auto const& stroke : strokes) o.add_bytes(5, stroke);
            });
            std::vector<std::uint8_t> data{ 'w', 'r', 'd', 0xf0, 1, 0 };
            data.insert(data.end(), body.begin(), body.end());
            return data;
        }

        std::string Square(std::vector<std::pair<float, float>> const& corners)
        {
            std::string s;
            for (auto [x, y] : corners)
            {
                Append(s, x);
                Append(s, y);
            }
            return s;
        }

        std::vector<std::pair<float, float>> const Line{ { 10.f, 10.f }, { 20.f, 10.f }, { 30.f, 10.f } };

        std::vector<std::uint8_t> SampleDrawing()
        {
            auto stroke = [](Writer const& w) { return Message(w); };
            return Wrd({ Ink("com.apple.ink.pen", 0, 0, 1, 1), Ink("com.apple.ink.marker", 1, 1, 0, 0.6f) },
                       {
                           // plain pen stroke
                           stroke([](protozero::pbf_writer& o) { o.add_uint64(4, 0); o.add_bytes(5, Path(Line, 2.5f)); }),
                           // moved marker stroke
                           stroke([](protozero::pbf_writer& o) {
                               o.add_uint64(4, 1);
                               o.add_bytes(5, Path(Line, 5));
                               o.add_bytes(7, Message([](protozero::pbf_writer& t) {
                                   t.add_float(1, 1); t.add_float(2, 0); t.add_float(3, 0); t.add_float(4, 1); t.add_float(5, 0); t.add_float(6, 100);
                               }));
                           }),
                           // partly erased: the range of its first point and a half
                           stroke([](protozero::pbf_writer& o) {
                               o.add_uint64(4, 0);
                               o.add_bytes(5, Path(Line, 2.5f));
                               o.add_bytes(11, Message([](protozero::pbf_writer& p) { p.add_float(12, 0); p.add_float(12, 1.5f); }));
                           }),
                           // erased: a mask beside all its points
                           stroke([](protozero::pbf_writer& o) {
                               o.add_uint64(4, 0);
                               o.add_bytes(5, Path(Line, 2.5f));
                               o.add_bytes(11, Message([](protozero::pbf_writer& p) { p.add_bytes(10, Square({ { 50.f, 50.f }, { 60.f, 50.f }, { 60.f, 60.f }, { 50.f, 60.f } })); }));
                           }),
                           // clipped by a mask: only x 15..25 stays
                           stroke([](protozero::pbf_writer& o) {
                               o.add_uint64(4, 0);
                               o.add_bytes(5, Path(Line, 2.5f));
                               o.add_bytes(11, Message([](protozero::pbf_writer& p) { p.add_bytes(10, Square({ { 15.f, 0.f }, { 25.f, 0.f }, { 25.f, 20.f }, { 15.f, 20.f } })); }));
                           }),
                           // deleted (no path)
                           stroke([](protozero::pbf_writer& o) { o.add_uint64(4, 0); }),
                       });
        }

        // ------------------------------------------------------------------ binary plist writer (bplist00)

        struct Plist;
        using PlistPtr = std::shared_ptr<Plist>;
        struct Plist
        {
            struct Uid { std::uint64_t value; };
            std::variant<std::string, std::vector<std::uint8_t>, std::int64_t, double, Uid, std::vector<PlistPtr>,
                         std::vector<std::pair<std::string, PlistPtr>>> value;
        };
        PlistPtr Str(std::string s) { return std::make_shared<Plist>(Plist{ std::move(s) }); }
        PlistPtr Data(std::vector<std::uint8_t> d) { return std::make_shared<Plist>(Plist{ std::move(d) }); }
        PlistPtr Int(std::int64_t v) { return std::make_shared<Plist>(Plist{ v }); }
        PlistPtr Real(double v) { return std::make_shared<Plist>(Plist{ v }); }
        PlistPtr Ref(std::uint64_t v) { return std::make_shared<Plist>(Plist{ Plist::Uid{ v } }); }
        PlistPtr Arr(std::vector<PlistPtr> items) { return std::make_shared<Plist>(Plist{ std::move(items) }); }
        PlistPtr Dict(std::vector<std::pair<std::string, PlistPtr>> entries) { return std::make_shared<Plist>(Plist{ std::move(entries) }); }

        class PlistWriter
        {
        public:
            std::vector<std::uint8_t> Write(PlistPtr const& root)
            {
                Add(root);
                std::vector<std::uint8_t> out{ 'b', 'p', 'l', 'i', 's', 't', '0', '0' };
                std::vector<std::uint32_t> offsets;
                for (auto const& o : objects)
                {
                    offsets.push_back(static_cast<std::uint32_t>(out.size()));
                    out.insert(out.end(), o.begin(), o.end());
                }
                auto table = static_cast<std::uint64_t>(out.size());
                for (auto offset : offsets) Big(out, offset, 4);
                out.insert(out.end(), 6, 0);
                out.push_back(4); // offset size
                out.push_back(1); // reference size (fewer than 256 objects)
                Big(out, objects.size(), 8);
                Big(out, 0, 8);   // top object
                Big(out, table, 8);
                return out;
            }

        private:
            static void Big(std::vector<std::uint8_t>& out, std::uint64_t v, int size)
            {
                for (int i = size - 1; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
            }
            static void Marker(std::vector<std::uint8_t>& out, std::uint8_t high, size_t length)
            {
                if (length < 15)
                {
                    out.push_back(static_cast<std::uint8_t>(high << 4 | length));
                    return;
                }
                out.push_back(static_cast<std::uint8_t>(high << 4 | 0x0F));
                out.push_back(0x12);
                Big(out, length, 4);
            }
            size_t Add(PlistPtr const& node)
            {
                size_t number = objects.size();
                objects.emplace_back();
                std::vector<std::uint8_t> o;
                std::visit([&](auto const& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>) { Marker(o, 0x5, v.size()); o.insert(o.end(), v.begin(), v.end()); }
                    else if constexpr (std::is_same_v<T, std::vector<std::uint8_t>>) { Marker(o, 0x4, v.size()); o.insert(o.end(), v.begin(), v.end()); }
                    else if constexpr (std::is_same_v<T, std::int64_t>) { o.push_back(0x13); Big(o, static_cast<std::uint64_t>(v), 8); }
                    else if constexpr (std::is_same_v<T, double>) { o.push_back(0x23); Big(o, std::bit_cast<std::uint64_t>(v), 8); }
                    else if constexpr (std::is_same_v<T, Plist::Uid>) { o.push_back(0x80); o.push_back(static_cast<std::uint8_t>(v.value)); }
                    else if constexpr (std::is_same_v<T, std::vector<PlistPtr>>)
                    {
                        std::vector<size_t> refs;
                        for (auto const& item : v) refs.push_back(Add(item));
                        Marker(o, 0xA, refs.size());
                        for (auto r : refs) o.push_back(static_cast<std::uint8_t>(r));
                    }
                    else
                    {
                        std::vector<size_t> keys, values;
                        for (auto const& [k, value] : v)
                        {
                            keys.push_back(Add(Str(k)));
                            values.push_back(Add(value));
                        }
                        Marker(o, 0xD, keys.size());
                        for (auto r : keys) o.push_back(static_cast<std::uint8_t>(r));
                        for (auto r : values) o.push_back(static_cast<std::uint8_t>(r));
                    }
                }, node->value);
                objects[number] = std::move(o);
                return number;
            }
            std::vector<std::vector<std::uint8_t>> objects;
        };

        std::string Base64(std::vector<std::uint8_t> const& data)
        {
            DWORD size = 0;
            CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
            std::string text(size, '\0');
            CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &size);
            text.resize(size);
            return text;
        }

        // An NSKeyedArchiver archive as Apple's markup writes it (root -> $class, drawing, drawingSize).
        std::string Archive(std::string const& className, std::optional<std::vector<std::uint8_t>> const& drawing)
        {
            std::vector<std::pair<std::string, PlistPtr>> root{ { "$class", Ref(2) } };
            if (drawing)
            {
                root.emplace_back("drawing", Ref(3));
                root.emplace_back("drawingSize", Ref(4));
            }
            auto objects = Arr({ Str("$null"), Dict(root), Dict({ { "$classname", Str(className) } }), Data(drawing.value_or(std::vector<std::uint8_t>{})),
                                 Dict({ { "NS.keys", Arr({ Ref(5), Ref(6) }) }, { "NS.objects", Arr({ Ref(7), Ref(8) }) } }),
                                 Str("Width"), Str("Height"), Real(595.276), Real(841.89) });
            auto archive = Dict({ { "$archiver", Str("NSKeyedArchiver") }, { "$top", Dict({ { "root", Ref(1) } }) },
                                  { "$objects", objects }, { "$version", Int(100000) } });
            return Base64(PlistWriter().Write(archive));
        }

        // ------------------------------------------------------------------ the marked-up PDF (qpdf)

        QPDFObjectHandle Form(QPDF& pdf, double width, double height, std::string const& content)
        {
            auto form = pdf.newStream(content);
            auto d = form.getDict();
            d.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
            d.replaceKey("/Subtype", QPDFObjectHandle::newName("/Form"));
            d.replaceKey("/BBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, width, height)));
            return form;
        }

        QPDFObjectHandle Annotation(QPDF& pdf, char const* subtype, double x1, double y1, double x2, double y2, QPDFObjectHandle appearance)
        {
            auto a = QPDFObjectHandle::newDictionary();
            a.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
            a.replaceKey("/Subtype", QPDFObjectHandle::newName(subtype));
            a.replaceKey("/Rect", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(x1, y1, x2, y2)));
            a.replaceKey("/F", QPDFObjectHandle::newInteger(4));
            if (appearance.isInitialized())
            {
                auto ap = QPDFObjectHandle::newDictionary();
                ap.replaceKey("/N", appearance);
                a.replaceKey("/AP", ap);
            }
            return pdf.makeIndirectObject(a);
        }

        QPDFObjectHandle Extras(std::vector<std::pair<std::string, std::string>> const& entries)
        {
            auto extras = QPDFObjectHandle::newDictionary();
            for (auto const& [key, value] : entries) extras.replaceKey(key, QPDFObjectHandle::newString(value));
            return extras;
        }

        // An A4 page with: handwriting (an ink annotation with its drawing), the stale copy iPadOS leaves of it, a text
        // box, a stamp with only an appearance, a screenshot over it, a drawing that cannot be read, a link.
        std::filesystem::path MarkedUpPdf(std::filesystem::path const& folder)
        {
            auto path = folder / L"ue1.pdf";
            QPDF pdf;
            pdf.emptyPDF();
            std::vector<QPDFObjectHandle> annotations;
            auto ink = Annotation(pdf, "/Stamp", 0, 700, 100, 841.89, Form(pdf, 100, 141.89, "0 0 1 RG 10 131 m 30 131 l S"));
            ink.replaceKey("/AAPL:AKExtras", Extras({ { "/AAPL:AKAnnotationV2", Archive("AKInkAnnotation2", SampleDrawing()) } }));
            annotations.push_back(ink);
            auto copy = Annotation(pdf, "/Stamp", 0, 700, 100, 841.89, Form(pdf, 100, 141.89, "0 0 1 RG 10 131 m 30 131 l S"));
            copy.replaceKey("/AAPL:AKExtras", Extras({ { "/PPKID", "AkHO" } }));
            annotations.push_back(copy);
            auto text = Annotation(pdf, "/FreeText", 248, 405, 378, 424, Form(pdf, 130, 19, "BT /F1 1 Tf 11 0 0 11 8.5 5.65 Tm (Name) Tj ET"));
            text.replaceKey("/Contents", QPDFObjectHandle::newString("Name"));
            text.replaceKey("/DA", QPDFObjectHandle::newString("/Helvetica 12 Tf 0 g"));
            annotations.push_back(text);
            annotations.push_back(Annotation(pdf, "/Stamp", 300, 300, 400, 350, Form(pdf, 100, 50, "0 0 0 rg 0 0 100 50 re f")));
            annotations.push_back(Annotation(pdf, "/Stamp", 350, 320, 450, 370, Form(pdf, 100, 50, "1 0 0 rg 0 0 100 50 re f")));
            auto damaged = Annotation(pdf, "/Stamp", 100, 100, 200, 150, Form(pdf, 100, 50, "0 0 0 RG 0 0 m 100 50 l S"));
            damaged.replaceKey("/AAPL:AKExtras", Extras({ { "/AAPL:AKAnnotationV2", Archive("AKInkAnnotation2", std::vector<std::uint8_t>{ 'w', 'r', 'd', 0xf0, 1, 0, 0x2a, 0xff }) } }));
            annotations.push_back(damaged);
            annotations.push_back(Annotation(pdf, "/Link", 50, 50, 100, 60, QPDFObjectHandle()));

            auto page = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, 595.276, 841.89)));
            page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
            page.replaceKey("/Contents", pdf.newStream(std::string()));
            auto array = QPDFObjectHandle::newArray();
            for (auto const& a : annotations) array.appendItem(a);
            page.replaceKey("/Annots", array);
            pdf.addPage(pdf.makeIndirectObject(page), false);
            std::string utf8 = ToUtf8(path.wstring());
            QPDFWriter writer(pdf, utf8.c_str());
            writer.write();
            return path;
        }

        std::vector<std::string> Subtypes(std::filesystem::path const& file)
        {
            auto pdf = Open(file);
            std::vector<std::string> subtypes;
            auto annots = pdf.pages[0].getObjectHandle().getKey("/Annots");
            if (annots.isArray())
            {
                for (auto& a : annots.aitems()) subtypes.push_back(a.getKey("/Subtype").getName());
            }
            return subtypes;
        }

        std::wstring Attr(XmlElementPtr const& e, std::wstring_view name) { return e->AttributeOr(name, L""); }
        double Number(XmlElementPtr const& e, std::wstring_view name) { return XoppFormat::ParseDouble(Attr(e, name)).value_or(-1); }
    }

    TEST(PencilKit, StrokesWithTransformsErasedRangesAndMasks)
    {
        auto strokes = PencilKitDrawing::Read(SampleDrawing());
        ASSERT_EQ(strokes.size(), 4u);
        auto const& pen = strokes[0];
        EXPECT_EQ(pen.ink, "com.apple.ink.pen");
        EXPECT_FALSE(pen.IsMarker());
        EXPECT_NEAR(pen.width, 2.5, 1e-3);
        EXPECT_EQ(pen.points, (std::vector<Point>{ { 10, 10 }, { 20, 10 }, { 30, 10 } }));
        EXPECT_EQ(pen.color, (Color{ 255, 0, 0, 255 }));

        auto const& marker = strokes[1];
        EXPECT_TRUE(marker.IsMarker());
        EXPECT_NEAR(marker.points[0].y, 110, 1e-3); // moved by its transform
        EXPECT_EQ(marker.color.a, 153);

        auto const& cut = strokes[2];
        EXPECT_NEAR(cut.points.front().x, 10, 1e-3);
        EXPECT_NEAR(cut.points.back().x, 25, 1e-3); // parameter 1.5: halfway from the 2nd to the 3rd point

        auto const& clipped = strokes[3];
        EXPECT_GE(clipped.points.front().x, 15);
        EXPECT_LE(clipped.points.front().x, 15.3);
        EXPECT_GE(clipped.points.back().x, 24.7);
        EXPECT_LE(clipped.points.back().x, 25);
    }

    TEST(PencilKit, OlderGzipDrawings)
    {
        auto point = [](float x, float y) {
            std::string p;
            Append(p, 0.0f);
            Append(p, x);
            Append(p, y);
            for (std::uint16_t v : std::initializer_list<std::uint16_t>{ 25, 1000, 0, 1000, 0, 0 }) Append(p, v);
            return p;
        };
        std::string drawing = Message([&](protozero::pbf_writer& o) {
            o.add_bytes(4, Ink("com.apple.ink.pen", 1, 0, 0, 1));
            o.add_bytes(5, Message([&](protozero::pbf_writer& s) { s.add_uint64(3, 0); s.add_uint64(4, 2); s.add_bytes(5, point(1, 2) + point(3, 4)); }));
        });
        std::string root = Message([&](protozero::pbf_writer& o) {
            o.add_uint64(1, 0);
            o.add_bytes(2, Message([&](protozero::pbf_writer& m) { m.add_bytes(3, drawing); }));
        });
        auto gzipped = io::Gzip::Compress(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const*>(root.data()), root.size()));

        auto strokes = PencilKitDrawing::Read(gzipped);
        ASSERT_EQ(strokes.size(), 1u);
        EXPECT_EQ(strokes[0].points, (std::vector<Point>{ { 1, 2 }, { 3, 4 } }));
        EXPECT_NEAR(strokes[0].width, 2.5, 1e-3);
    }

    TEST(PencilKit, DataThatIsNoDrawingThrows)
    {
        EXPECT_THROW(PencilKitDrawing::Read(std::vector<std::uint8_t>{ 1, 2, 3 }), Error);
        EXPECT_THROW(PencilKitDrawing::Read(std::vector<std::uint8_t>{ 'w', 'r', 'd', 0xf0, 1, 0, 0xff, 0xff }), Error);
    }

    TEST(BinaryPlist, ReadsObjectsAndRejectsDamagedData)
    {
        std::string longText(40, 'x');
        auto bytes = PlistWriter().Write(Dict({ { "text", Str(longText) }, { "n", Int(-5) }, { "r", Real(1.5) }, { "list", Arr({ Ref(7), Data({ 1, 2 }) }) } }));
        import::detail::BinaryPlist plist(bytes);
        auto top = plist.Object(plist.Top());
        auto const& dict = std::get<import::detail::BinaryPlist::Dictionary>(top);
        EXPECT_EQ(std::get<std::wstring>(plist.Object(*plist.Find(dict, L"text"))), std::wstring(40, L'x'));
        EXPECT_EQ(std::get<std::int64_t>(plist.Object(*plist.Find(dict, L"n"))), -5);
        EXPECT_EQ(std::get<double>(plist.Object(*plist.Find(dict, L"r"))), 1.5);
        auto list = std::get<import::detail::BinaryPlist::Array>(plist.Object(*plist.Find(dict, L"list")));
        ASSERT_EQ(list.items.size(), 2u);
        EXPECT_EQ(std::get<import::detail::BinaryPlist::Uid>(plist.Object(list.items[0])).value, 7u);
        EXPECT_FALSE(plist.Find(dict, L"missing"));

        EXPECT_THROW(import::detail::BinaryPlist(std::vector<std::uint8_t>{ 'b', 'p' }), Error);
        auto cut = bytes;
        cut[cut.size() - 1] = 0xFF; // the offset table beyond the data
        EXPECT_THROW(import::detail::BinaryPlist{ cut }, Error);
    }

    TEST(AppleMarkupArchive, NamesItsClassAndDrawing)
    {
        auto markup = AppleMarkup::Read(Archive("AKInkAnnotation2", std::vector<std::uint8_t>{ 1, 2, 3 }));
        EXPECT_EQ(markup.className, AppleMarkup::InkClass);
        ASSERT_TRUE(markup.drawing);
        EXPECT_EQ(*markup.drawing, (std::vector<std::uint8_t>{ 1, 2, 3 }));
        EXPECT_NEAR(markup.drawingWidth, 595.276, 1e-3);
        EXPECT_NEAR(markup.drawingHeight, 841.89, 1e-3);
        EXPECT_FALSE(AppleMarkup::Read(Archive("AKImageAnnotation", std::nullopt)).drawing);
        EXPECT_THROW(AppleMarkup::Read("bm90IGEgcGxpc3Q="), Error); // "not a plist"
    }

    TEST(MarkupImport, TakesHandwritingTextsAndPicturesOutOfThePdf)
    {
        TempFolder folder;
        auto source = MarkedUpPdf(folder.Path());
        auto before = ReadBytes(source);
        auto target = folder / L"out.pdf";
        auto pictures = [&](int round) { return folder / std::format(L"pictures-{}.pdf", round); };

        auto import = Prepare(source, target, pictures);

        EXPECT_EQ(ReadBytes(source), before);                                       // the source stays as it is
        EXPECT_EQ(Subtypes(target), (std::vector<std::string>{ "/Link" }));           // the markup is gone, the link stays
        EXPECT_EQ(Subtypes(pictures(0)), (std::vector<std::string>{ "/Stamp", "/Stamp" })); // only what becomes a picture
        EXPECT_EQ(Subtypes(pictures(1)), (std::vector<std::string>{ "/Stamp" }));     // the overlapping one, on its own
        ASSERT_EQ(import.pageSizes.size(), 1u);
        EXPECT_NEAR(import.pageSizes[0].width, 595.276, 1e-6);
        EXPECT_NEAR(import.pageSizes[0].height, 841.89, 1e-6);
        EXPECT_EQ(import.drawings, 1);
        EXPECT_EQ(import.texts, 1);
        EXPECT_EQ(import.kept, 0);

        auto const& elements = import.elements[0];
        std::vector<XmlElementPtr> strokes, texts;
        for (auto const& e : elements)
        {
            if (e->Name() == L"stroke") strokes.push_back(e);
            if (e->Name() == L"text") texts.push_back(e);
        }
        ASSERT_EQ(strokes.size(), 4u); // the stale copy brought nothing
        EXPECT_EQ(Attr(strokes[0], L"tool"), L"pen");
        EXPECT_EQ(Attr(strokes[0], L"color"), L"#0000ffff");
        EXPECT_EQ(Attr(strokes[0], L"width"), L"1"); // PencilKit's 2.5 as drawn
        EXPECT_EQ(strokes[0]->Value(), L"10 10 20 10 30 10");
        EXPECT_EQ(Attr(strokes[1], L"tool"), L"highlighter");
        EXPECT_EQ(Attr(strokes[1], L"color"), L"#ffff007f");
        EXPECT_EQ(Attr(strokes[1], L"width"), L"6");

        ASSERT_EQ(texts.size(), 1u);
        EXPECT_EQ(texts[0]->Value(), L"Name");
        EXPECT_EQ(Attr(texts[0], L"size"), L"11"); // as drawn, not the /DA's 12
        EXPECT_NEAR(Number(texts[0], L"x"), 256.5, 0.1);
        EXPECT_NEAR(Number(texts[0], L"y"), 841.89 - 410.65 - 0.905 * 11, 0.1);

        ASSERT_EQ(import.pictures.size(), 3u);
        auto const& picture = import.pictures[0];
        EXPECT_NEAR(picture.area.x, 300, 1e-6);
        EXPECT_NEAR(picture.area.y, 841.89 - 350, 1e-6);
        EXPECT_NEAR(picture.area.width, 100, 1e-6);
        EXPECT_NEAR(picture.area.height, 50, 1e-6);
        EXPECT_EQ(picture.pdf, pictures(0));
        EXPECT_EQ(import.pictures[1].pdf, pictures(1));
        EXPECT_EQ(import.pictures[2].pdf, pictures(0)); // the unreadable drawing overlaps nothing
        EXPECT_NE(std::find(elements.begin(), elements.end(), picture.element), elements.end()); // in the annotations' order

        // The document: the PDF as background, the elements on its page (a picture without data is left out).
        auto doc = CreateDocument(import, target);
        auto xopp = folder / L"out.xopp";
        io::DocumentFile::Save(*doc, xopp);
        auto loaded = io::DocumentFile::Load(xopp);
        ASSERT_TRUE(loaded->PdfPath());
        EXPECT_TRUE(Paths::Same(*loaded->PdfPath(), target));
        ASSERT_EQ(loaded->Pages().size(), 1u);
        auto page = loaded->Pages()[0]->Snapshot();
        auto layer = page->Element(L"layer");
        ASSERT_TRUE(layer);
        EXPECT_EQ(layer->Elements(L"stroke").size(), 4u);
        EXPECT_EQ(layer->Elements(L"text").size(), 1u);
        EXPECT_TRUE(layer->Elements(L"image").empty());
    }

    TEST(MarkupImport, RendersThePictures)
    {
        TempFolder folder;
        auto source = MarkedUpPdf(folder.Path());
        auto import = Prepare(source, folder / L"out.pdf", [&](int round) { return folder / std::format(L"pictures-{}.pdf", round); });

        RenderPictures(import, 2);

        for (auto const& picture : import.pictures)
        {
            EXPECT_FALSE(picture.element->Value().empty());
            auto data = XoppImage::GetData(*picture.element);
            ASSERT_GT(data.size(), 8u);
            EXPECT_EQ(data[1], 'P'); // PNG
            auto bounds = XoppImage::GetBounds(*picture.element);
            EXPECT_NEAR(bounds.x, picture.area.x, 1e-3);
            EXPECT_NEAR(bounds.width, picture.area.width, 1e-3);
        }
        auto doc = CreateDocument(import, folder / L"out.pdf");
        size_t images = 0;
        for (auto const& e : doc->Pages()[0]->Images().List()) images += e ? 1 : 0;
        EXPECT_EQ(images, 3u);
    }

    // ------------------------------------------------------------------ the iPad export

    TEST(BinaryPlistWriter, WritesWhatTheReaderReads)
    {
        using import::detail::BinaryPlist;
        import::detail::BinaryPlistWriter w;
        auto key = w.String("drawing");
        auto data = w.Data(std::vector<std::uint8_t>(40, 7));
        auto name = w.String("\xC3\xA4rger"); // UTF-8 of a-umlaut: written as UTF-16
        auto list = w.Array({ w.Int(5), w.Int(70000), w.Real(2.5), w.Bool(true), w.Uid(3) });
        auto top = w.Dictionary({ { key, data }, { w.String("name"), name }, { w.String("list"), list } });
        auto bytes = w.Finish(top);

        BinaryPlist plist(bytes);
        auto root = std::get<BinaryPlist::Dictionary>(plist.Object(plist.Top()));
        auto drawing = plist.Find(root, L"drawing");
        ASSERT_TRUE(drawing);
        EXPECT_EQ(std::get<BinaryPlist::Bytes>(plist.Object(*drawing)).size(), 40u);
        EXPECT_EQ(std::get<std::wstring>(plist.Object(*plist.Find(root, L"name"))), L"ärger");
        auto items = std::get<BinaryPlist::Array>(plist.Object(*plist.Find(root, L"list"))).items;
        ASSERT_EQ(items.size(), 5u);
        EXPECT_EQ(std::get<std::int64_t>(plist.Object(items[1])), 70000);
        EXPECT_EQ(std::get<double>(plist.Object(items[2])), 2.5);
        EXPECT_EQ(std::get<BinaryPlist::Uid>(plist.Object(items[4])).value, 3u);
    }

    TEST(PencilKitWriter, WritesWhatTheReaderReadsInTheLayoutOfIpadOs)
    {
        PencilKitStroke pen;
        pen.ink = "com.apple.ink.pen";
        pen.color = Color{ 0xFF, 0, 0, 0xFF };
        pen.points = { { 10, 20 }, { 30, 40 }, { 50, 20 } };
        pen.width = 2.5;
        PencilKitStroke red = pen;
        red.color = Color{ 0xFF, 0xFF, 0, 0 };
        red.points = { { 100, 100 }, { 110, 105 } };
        red.widths = { 3, 6 };
        std::vector<PencilKitStroke> strokes{ pen, red };

        auto data = PencilKitDrawing::WriteLegacy(strokes);

        auto read = PencilKitDrawing::Read(data);
        ASSERT_EQ(read.size(), 2u);
        EXPECT_EQ(read[0].color, pen.color);
        EXPECT_EQ(read[1].color, red.color);
        ASSERT_EQ(read[0].points.size(), 3u);
        EXPECT_NEAR(read[0].points[1].x, 30, 1e-4);
        EXPECT_NEAR(read[0].points[1].y, 40, 1e-4);
        EXPECT_NEAR(read[0].width, 2.5, 1e-9);
        EXPECT_NEAR(read[1].width, 6, 1e-9); // the median of 3 and 6 (the second one of two)

        // The layout of the user's drawings: a gzip of {1: 0, 2: {1: 10, 2: 10, 3: drawing}}; the drawing has the version,
        // two ids, the ordering records {n,0,n} and {0,1,0}, the inks (#4), the strokes (#5) with {i,0,i+1}, {0,0,0}, the
        // ink index, the point count and the 24-byte points, then an id and {0,1,0}.
        auto raw = io::Gzip::Decompress(data);
        protozero::pbf_reader root(reinterpret_cast<char const*>(raw.data()), raw.size());
        ASSERT_TRUE(root.next(1));
        EXPECT_EQ(root.get_uint32(), 0u);
        ASSERT_TRUE(root.next(2));
        auto container = root.get_message();
        ASSERT_TRUE(container.next(1));
        EXPECT_EQ(container.get_uint32(), 10u);
        ASSERT_TRUE(container.next(2));
        EXPECT_EQ(container.get_uint32(), 10u);
        ASSERT_TRUE(container.next(3));
        auto drawing = container.get_message();
        std::vector<int> order;
        int inks = 0, strokeCount = 0;
        while (drawing.next())
        {
            order.push_back(static_cast<int>(drawing.tag()));
            if (drawing.tag() == 4) ++inks;
            if (drawing.tag() == 5)
            {
                auto stroke = drawing.get_message();
                std::vector<int> fields;
                while (stroke.next())
                {
                    fields.push_back(static_cast<int>(stroke.tag()));
                    if (stroke.tag() == 5) EXPECT_EQ(stroke.get_view().size(), (strokeCount == 0 ? 3u : 2u) * 24);
                    else stroke.skip();
                }
                EXPECT_EQ(fields, (std::vector<int>{ 1, 2, 3, 4, 5, 6, 9, 11, 13, 14 }));
                ++strokeCount;
            }
            else drawing.skip();
        }
        EXPECT_EQ(order, (std::vector<int>{ 1, 2, 2, 3, 3, 4, 4, 5, 5, 9, 10 }));
        EXPECT_EQ(inks, 2);
        EXPECT_THROW(PencilKitDrawing::WriteLegacy({}), Error);
    }

    TEST(AppleMarkupArchive, WrittenInkReadsBack)
    {
        std::vector<std::uint8_t> drawing{ 1, 2, 3, 4 };
        auto text = AppleMarkup::WriteInk(drawing, 595.276, 841.89, 40.5, 700.25, 81, 85, "196366F7-B305-4FFB-BC01-9897D82136CA");
        auto markup = AppleMarkup::Read(text);
        EXPECT_EQ(markup.className, AppleMarkup::InkClass);
        ASSERT_TRUE(markup.drawing);
        EXPECT_EQ(*markup.drawing, drawing);
        EXPECT_NEAR(markup.drawingWidth, 595.276, 1e-9);
        EXPECT_NEAR(markup.drawingHeight, 841.89, 1e-9);
    }

    TEST(IpadExport, WritesPenStrokesAsEditableInkAndLeavesTheRestInThePage)
    {
        TempFolder folder;
        auto source = folder / L"notes.xopp";
        WriteBytes(source, R"(<?xml version="1.0" standalone="no"?>
<xournal creator="xournalpp 1.3.8" fileversion="5">
<page width="400" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#3333ccff" width="1.2">10 20 30 40 50 20</stroke>
<stroke tool="pen" color="#ff0000ff" width="1.5 1 2">100 100 110 105 130 110</stroke>
<stroke tool="highlighter" color="#ffff007f" width="8">20 200 120 200</stroke>
<text font="Sans" size="12" x="10" y="250" color="#000000ff">Hallo</text>
</layer></page>
<page width="400" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer/></page>
</xournal>)");
        auto doc = io::DocumentFile::Load(source);
        auto target = folder / L"ipad.pdf";

        auto result = import::ExportForIpad(pdf::PrepareExport(*doc), target);

        EXPECT_EQ(result.pdf.pages, 2);
        EXPECT_EQ(result.drawings, 1);
        EXPECT_EQ(result.strokes, 2);
        EXPECT_EQ(result.flatStrokes, 1); // the highlighter stays in the page
        EXPECT_EQ(result.strokes + result.flatStrokes, 3); // every stroke of the page is somewhere, once
        EXPECT_EQ(Subtypes(target), (std::vector<std::string>{ "/Stamp" }));

        // Imported again (as an iPad PDF): the pen strokes are strokes again, where and as wide as they were.
        auto imported = Prepare(target, folder / L"clean.pdf", [&](int round) { return folder / std::format(L"pictures-{}.pdf", round); });
        EXPECT_EQ(imported.drawings, 1);
        std::vector<XmlElementPtr> strokes;
        for (auto const& e : imported.elements[0]) if (e->Name() == L"stroke") strokes.push_back(e);
        ASSERT_EQ(strokes.size(), 2u);
        EXPECT_EQ(Attr(strokes[0], L"color"), L"#3333ccff");
        EXPECT_NEAR(Number(strokes[0], L"width"), std::sqrt(1.2), 0.02); // the brush size follows the square root of the width (stored in tenths)
        auto coordinates = [](XmlElementPtr const& e)
        {
            std::vector<double> numbers;
            std::wstring text = e->Value();
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find(L' ', at);
                if (end == std::wstring::npos) end = text.size();
                numbers.push_back(XoppFormat::ParseDouble(text.substr(at, end - at)).value_or(-1));
                at = end + 1;
            }
            return numbers;
        };
        // PencilKit keeps the control points of a smooth curve: the polyline's few points come back as many (as dense as
        // the points of the user's own drawings: never more than 2 pt apart), from the first point to the last.
        auto spacing = [&](std::vector<double> const& c)
        {
            double widest = 0;
            for (size_t i = 2; i + 1 < c.size(); i += 2) widest = std::max(widest, std::hypot(c[i] - c[i - 2], c[i + 1] - c[i - 1]));
            return widest;
        };
        auto first = coordinates(strokes[0]);
        ASSERT_GT(first.size(), 60u);
        EXPECT_NEAR(first[0], 10, 1e-2);
        EXPECT_NEAR(first[1], 20, 1e-2);
        EXPECT_NEAR(first[first.size() - 2], 50, 1e-2);
        EXPECT_NEAR(first[first.size() - 1], 20, 1e-2);
        EXPECT_LE(spacing(first), 2.0);
        auto second = coordinates(strokes[1]);
        EXPECT_NEAR(second[second.size() - 2], 130, 1e-2);
        EXPECT_NEAR(second[second.size() - 1], 110, 1e-2);
        EXPECT_LE(spacing(second), 2.0);
    }

    TEST(IpadExport, StrokesThatCannotLieUnderWhatIsAboveThemStayInThePage)
    {
        TempFolder folder;
        auto source = folder / L"notes.xopp";
        WriteBytes(source, R"(<?xml version="1.0" standalone="no"?>
<xournal creator="xournalpp 1.3.8" fileversion="5">
<page width="400" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#000000ff" width="1.2">10 20 30 40</stroke>
<stroke tool="pen" color="#000000ff" width="1.2">-20 100 30 110</stroke>
<stroke tool="pen" color="#000000ff" width="1.2">100 100 140 100</stroke>
<stroke tool="pen" color="#000000ff" width="1.2">200 200 240 210</stroke>
<stroke tool="pen" color="#000000ff" width="1.2" fill="128">300 20 340 20</stroke>
<text font="Sans" size="12" x="95" y="95" color="#000000ff">Deckt zu</text>
<stroke tool="pen" color="#ff0000ff" width="1.2" style="dash">150 200 250 200</stroke>
</layer></page>
</xournal>)");
        auto doc = io::DocumentFile::Load(source);

        auto result = import::ExportForIpad(pdf::PrepareExport(*doc), folder / L"ipad.pdf");

        // The first one is free; the second leaves the page; the third lies under a text; the fourth lies under a dashed
        // stroke that covers it; the filled one is drawn as before.
        EXPECT_EQ(result.strokes + result.flatStrokes, 6);
        EXPECT_EQ(result.strokes, 1);
    }

    TEST(IpadExport, ATapKeepsItsDot)
    {
        TempFolder folder;
        auto source = folder / L"notes.xopp";
        WriteBytes(source, R"(<?xml version="1.0" standalone="no"?>
<xournal creator="xournalpp 1.3.8" fileversion="5">
<page width="400" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer>
<stroke tool="pen" color="#000000ff" width="1.4">133.8 234.2 133.8 234.2</stroke>
</layer></page>
</xournal>)");
        auto doc = io::DocumentFile::Load(source);
        auto target = folder / L"ipad.pdf";
        auto result = import::ExportForIpad(pdf::PrepareExport(*doc), target);
        EXPECT_EQ(result.strokes, 1);
        auto imported = Prepare(target, folder / L"clean.pdf", [&](int round) { return folder / std::format(L"pictures-{}.pdf", round); });
        ASSERT_EQ(imported.drawings, 1);
        for (auto const& e : imported.elements[0])
        {
            if (e->Name() != L"stroke") continue;
            // Many points within a fraction of a point of the tap.
            EXPECT_GT(e->Value().size(), 50u);
        }
    }

    // The user's kind of document (the sanitized example): every editable stroke comes back as a stroke. With
    // XOURNAL_PROBE_OUT set the exported PDF and a rendering of its pages are written there to look at.
    TEST(IpadExport, AnExampleDocumentComesBackAsItsStrokes)
    {
        TempFolder folder;
        auto doc = io::DocumentFile::Load(std::filesystem::path(XOURNAL_EXAMPLES_DIR) / L"Schaltungen.xopp");
        auto target = folder / L"ipad.pdf";

        auto result = import::ExportForIpad(pdf::PrepareExport(*doc), target);

        EXPECT_GT(result.strokes, 0);
        auto imported = Prepare(target, folder / L"clean.pdf", [&](int round) { return folder / std::format(L"pictures-{}.pdf", round); });
        int strokes = 0;
        for (auto const& page : imported.elements)
            for (auto const& e : page) strokes += e->Name() == L"stroke" ? 1 : 0;
        EXPECT_EQ(strokes, result.strokes);
        EXPECT_EQ(imported.drawings, result.drawings);

        wchar_t out[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"XOURNAL_PROBE_OUT", out, MAX_PATH) > 0)
        {
            std::filesystem::path dir(out);
            std::filesystem::copy_file(target, dir / L"ipad.pdf", std::filesystem::copy_options::overwrite_existing);
            pdf::PdfAreaRenderer renderer(target);
            for (size_t page = 0; page < imported.pageSizes.size(); ++page)
            {
                auto rendered = renderer.Render(static_cast<int>(page), Rect{ 0, 0, imported.pageSizes[page].width, imported.pageSizes[page].height }, 1.5);
                WriteBytes(dir / std::format(L"page{}.png", page + 1), std::string_view(reinterpret_cast<char const*>(rendered.png.data()), rendered.png.size()));
            }
        }
    }
}
