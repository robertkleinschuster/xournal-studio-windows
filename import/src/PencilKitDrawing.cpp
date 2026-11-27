#include "xournal/import/PencilKitDrawing.h"

#include "xournal/base/Error.h"
#include "xournal/io/Gzip.h"

#include <protozero/pbf_reader.hpp>
#include <protozero/pbf_writer.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstring>
#include <functional>
#include <format>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace xournal::import
{
    bool PencilKitStroke::IsMarker() const
    {
        constexpr std::string_view suffix = ".marker";
        if (ink.size() < suffix.size()) return false;
        return std::equal(suffix.begin(), suffix.end(), ink.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    }

    namespace
    {
        // Channel sizes in bit order (PKStrokePathPointsShared): location (2 f32), time (f32), width (f32), aspect, edge
        // width, force, azimuth, altitude, opacity (u16 each), secondary radius (f32), threshold (u16), elevation (f32).
        constexpr int ChannelSizes[] = { 8, 4, 4, 2, 2, 2, 2, 2, 2, 4, 2, 4 };
        constexpr int ChannelCount = 12;
        constexpr int LocationChannel = 0, WidthChannel = 2, OpacityChannel = 8;

        float F32(std::string_view data, size_t at)
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, data.data() + at, 4); // little endian, as x64
            return std::bit_cast<float>(bits);
        }

        std::uint16_t U16(std::string_view data, size_t at)
        {
            return static_cast<std::uint16_t>(static_cast<unsigned char>(data[at]) | static_cast<unsigned char>(data[at + 1]) << 8);
        }

        /// A protobuf message read field by field (the schema is not published), with protozero's reader. The views point
        /// into the drawing's data, which outlives every message of one Read.
        class Message
        {
        public:
            static Message Parse(std::string_view data)
            {
                Message message;
                protozero::pbf_reader reader(data.data(), data.size());
                while (reader.next())
                {
                    Field f{ reader.tag(), 0, false, {} };
                    switch (reader.wire_type())
                    {
                    case protozero::pbf_wire_type::varint: f.value = reader.get_uint64(); break;
                    case protozero::pbf_wire_type::fixed32: f.value = reader.get_fixed32(); break;
                    case protozero::pbf_wire_type::fixed64: f.value = reader.get_fixed64(); break;
                    case protozero::pbf_wire_type::length_delimited:
                    {
                        auto view = reader.get_view();
                        f.isBytes = true;
                        f.bytes = std::string_view(view.data(), view.size());
                        break;
                    }
                    default: ThrowInvalidData(std::format("PencilKit drawing: unexpected wire type in field {}", f.number));
                    }
                    message.fields.push_back(f);
                }
                return message;
            }

            std::optional<std::uint64_t> Varint(std::uint32_t number) const
            {
                for (auto const& f : fields)
                {
                    if (f.number == number && !f.isBytes) return f.value;
                }
                return std::nullopt;
            }

            std::optional<float> Float(std::uint32_t number) const
            {
                auto v = Varint(number);
                if (!v) return std::nullopt;
                return std::bit_cast<float>(static_cast<std::uint32_t>(*v));
            }

            /// Repeated floats, each its own field or packed into one (protobuf's packed encoding).
            std::vector<float> Floats(std::uint32_t number) const
            {
                std::vector<float> values;
                for (auto const& f : fields)
                {
                    if (f.number != number) continue;
                    if (!f.isBytes) values.push_back(std::bit_cast<float>(static_cast<std::uint32_t>(f.value)));
                    else for (size_t i = 0; i + 4 <= f.bytes.size(); i += 4) values.push_back(F32(f.bytes, i));
                }
                return values;
            }

            std::optional<std::string_view> Bytes(std::uint32_t number) const
            {
                for (auto const& f : fields)
                {
                    if (f.number == number && f.isBytes) return f.bytes;
                }
                return std::nullopt;
            }

            std::vector<std::string_view> AllBytes(std::uint32_t number) const
            {
                std::vector<std::string_view> all;
                for (auto const& f : fields)
                {
                    if (f.number == number && f.isBytes) all.push_back(f.bytes);
                }
                return all;
            }

            std::optional<Message> Child(std::uint32_t number) const
            {
                auto b = Bytes(number);
                if (!b) return std::nullopt;
                return Parse(*b);
            }

            std::vector<Message> Children(std::uint32_t number) const
            {
                std::vector<Message> children;
                for (auto b : AllBytes(number)) children.push_back(Parse(b));
                return children;
            }

        private:
            struct Field
            {
                std::uint32_t number;
                std::uint64_t value;
                bool isBytes;
                std::string_view bytes;
            };
            std::vector<Field> fields;
        };

        struct Ink
        {
            std::string identifier;
            Color color;
        };

        struct RawPoint
        {
            double x = 0, y = 0, width = 0, opacity = 1;
        };

        std::vector<Ink> Inks(Message const& drawing)
        {
            std::vector<Ink> inks;
            for (auto const& ink : drawing.Children(4))
            {
                auto c = ink.Child(1);
                auto channel = [&](std::uint32_t n, float fallback) {
                    float v = c ? c->Float(n).value_or(fallback) : fallback;
                    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255));
                };
                auto id = ink.Bytes(2);
                inks.push_back({ id ? std::string(*id) : std::string(), Color{ channel(4, 1), channel(1, 0), channel(2, 0), channel(3, 0) } });
            }
            return inks;
        }

        Ink const& InkAt(std::vector<Ink> const& inks, std::uint64_t index)
        {
            if (index >= inks.size()) ThrowInvalidData(std::format("PencilKit drawing: stroke refers to ink {} of {}", index, inks.size()));
            return inks[static_cast<size_t>(index)];
        }

        int Size(std::uint64_t mask)
        {
            int size = 0;
            for (int i = 0; i < ChannelCount; ++i)
            {
                if ((mask >> i) & 1) size += ChannelSizes[i];
            }
            return size;
        }

        // The channels of a record (location: x as -1, y as 0), decoded as libfreeform does.
        std::map<int, double> Channels(std::string_view record, std::uint64_t mask)
        {
            std::map<int, double> values;
            size_t offset = 0;
            for (int i = 0; i < ChannelCount; ++i)
            {
                if (((mask >> i) & 1) == 0) continue;
                switch (i)
                {
                case LocationChannel:
                    values[-1] = F32(record, offset);
                    values[0] = F32(record, offset + 4);
                    break;
                case 1: case 2: case 9: case 11:
                    values[i] = F32(record, offset);
                    break;
                default:
                {
                    double raw = U16(record, offset);
                    values[i] = (i == OpacityChannel || i == 10) ? raw / 32767.5 : raw / 1000;
                    break;
                }
                }
                offset += static_cast<size_t>(ChannelSizes[i]);
            }
            return values;
        }

        std::vector<RawPoint> Points(Message const& path)
        {
            auto count = path.Varint(3).value_or(0);
            auto perPoint = path.Varint(4);
            if (!perPoint) ThrowInvalidData("PencilKit drawing: path without channel mask");
            auto constant = path.Varint(5).value_or(0);
            if (((*perPoint | constant) >> ChannelCount) != 0 || (*perPoint & constant) != 0 || (*perPoint & 1) == 0)
            {
                ThrowInvalidData(std::format("PencilKit drawing: unexpected channel masks {:x}/{:x}", *perPoint, constant));
            }
            auto stride = static_cast<size_t>(Size(*perPoint));
            auto records = path.Bytes(7).value_or(std::string_view{});
            auto constants = path.Bytes(6).value_or(std::string_view{});
            if (count > records.size() || records.size() != count * stride || constants.size() != static_cast<size_t>(Size(constant)))
            {
                ThrowInvalidData("PencilKit drawing: point records do not match their masks");
            }
            auto shared = Channels(constants, constant);
            std::vector<RawPoint> points;
            for (size_t i = 0; i < count; ++i)
            {
                auto values = Channels(records.substr(i * stride, stride), *perPoint);
                auto value = [&](int channel, double fallback) {
                    if (auto it = values.find(channel); it != values.end()) return it->second;
                    if (auto it = shared.find(channel); it != shared.end()) return it->second;
                    return fallback;
                };
                points.push_back({ values[-1], values[LocationChannel], value(WidthChannel, 0), value(OpacityChannel, 1) });
            }
            return points;
        }

        // The part of the path between two parameters (point index plus fraction), interpolated at both ends.
        std::vector<RawPoint> Cut(std::vector<RawPoint> const& points, double start, double end)
        {
            std::vector<RawPoint> cut;
            if (points.empty() || end < start) return cut;
            auto at = [&](double t) {
                t = std::clamp(t, 0.0, static_cast<double>(points.size() - 1));
                auto i = static_cast<size_t>(std::floor(t));
                if (i >= points.size() - 1) return points.back();
                double f = t - static_cast<double>(i);
                auto const& a = points[i];
                auto const& b = points[i + 1];
                return RawPoint{ a.x + f * (b.x - a.x), a.y + f * (b.y - a.y), a.width + f * (b.width - a.width), a.opacity };
            };
            cut.push_back(at(start));
            for (auto i = static_cast<long long>(std::floor(start)) + 1; i < end && i < static_cast<long long>(points.size()); ++i)
            {
                if (i >= 0) cut.push_back(points[static_cast<size_t>(i)]);
            }
            cut.push_back(at(end));
            return cut;
        }

        std::vector<Point> Polygon(std::string_view data)
        {
            std::vector<Point> polygon;
            for (size_t i = 0; i + 8 <= data.size(); i += 8) polygon.push_back({ F32(data, i), F32(data, i + 4) });
            return polygon;
        }

        bool Contains(std::vector<Point> const& polygon, double x, double y)
        {
            bool inside = false;
            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
            {
                auto const& a = polygon[i];
                auto const& b = polygon[j];
                if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
            }
            return inside;
        }

        // The samples a point apart (as dense as the drawing's own points), with both ends.
        std::vector<RawPoint> Thinned(std::vector<RawPoint> const& run)
        {
            std::vector<RawPoint> kept{ run[0] };
            for (size_t i = 1; i < run.size(); ++i)
            {
                auto const& last = kept.back();
                if (i == run.size() - 1 || std::abs(run[i].x - last.x) + std::abs(run[i].y - last.y) >= 1) kept.push_back(run[i]);
            }
            return kept;
        }

        // The runs of the polyline inside the polygons (even-odd), sampled every quarter point along it.
        std::vector<std::vector<RawPoint>> Clip(std::vector<RawPoint> const& points, std::vector<std::vector<Point>> const& polygons)
        {
            constexpr double Step = 0.25;
            std::vector<std::vector<RawPoint>> runs;
            std::vector<RawPoint> run;
            auto inside = [&](RawPoint const& p) {
                int n = 0;
                for (auto const& polygon : polygons) n += Contains(polygon, p.x, p.y) ? 1 : 0;
                return n % 2 == 1;
            };
            auto visit = [&](RawPoint const& sample) {
                if (inside(sample)) run.push_back(sample);
                else if (!run.empty())
                {
                    runs.push_back(Thinned(run));
                    run.clear();
                }
            };
            for (size_t i = 0; i < points.size(); ++i)
            {
                if (i > 0)
                {
                    auto const& a = points[i - 1];
                    auto const& b = points[i];
                    auto n = static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / Step));
                    for (int k = 1; k < n; ++k)
                    {
                        double f = static_cast<double>(k) / n;
                        visit({ a.x + f * (b.x - a.x), a.y + f * (b.y - a.y), a.width, a.opacity });
                    }
                }
                visit(points[i]);
            }
            if (!run.empty()) runs.push_back(Thinned(run));
            return runs;
        }

        std::optional<Matrix> Transform(std::optional<Message> const& m)
        {
            if (!m) return std::nullopt;
            return Matrix{ m->Float(1).value_or(1), m->Float(2).value_or(0), m->Float(3).value_or(0),
                           m->Float(4).value_or(1), m->Float(5).value_or(0), m->Float(6).value_or(0) };
        }

        double Median(std::vector<double> values)
        {
            if (values.empty()) return 0;
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        }

        PencilKitStroke Stroke(Ink const& ink, std::vector<RawPoint> const& raw, std::optional<Matrix> const& transform)
        {
            Matrix m = transform.value_or(Matrix{});
            PencilKitStroke stroke{ ink.identifier, ink.color, {}, 0 };
            std::vector<double> widths, opacities;
            for (auto const& p : raw)
            {
                // As the C# app (System.Numerics, float): the point transformed in single precision.
                float x = static_cast<float>(p.x * m.m11 + p.y * m.m21 + m.m31);
                float y = static_cast<float>(p.x * m.m12 + p.y * m.m22 + m.m32);
                if (std::isfinite(x) && std::isfinite(y)) stroke.points.push_back({ x, y });
                if (p.width > 0 && std::isfinite(p.width)) widths.push_back(p.width);
                opacities.push_back(p.opacity);
            }
            double scale = std::sqrt(std::abs(m.m11 * m.m22 - m.m12 * m.m21));
            stroke.width = Median(widths) * scale;
            double opacity = std::clamp(Median(opacities), 0.0, 1.0);
            if (opacity < 0.999) stroke.color.a = static_cast<std::uint8_t>(std::lround(stroke.color.a * opacity));
            return stroke;
        }

        std::vector<PencilKitStroke> ReadWrd(Message const& drawing)
        {
            auto inks = Inks(drawing);
            std::vector<PencilKitStroke> strokes;
            for (auto const& record : drawing.Children(5))
            {
                auto path = record.Child(5);
                if (!path) continue; // a deleted stroke
                auto points = Points(*path);
                auto pieces = record.Children(11);
                if (pieces.empty())
                {
                    strokes.push_back(Stroke(InkAt(inks, record.Varint(4).value_or(0)), points, Transform(record.Child(7))));
                    continue;
                }
                // Partly erased: each visible piece is a range of the path (parametric: point index plus fraction; all of
                // it when none is given), shown only inside its mask polygons (stroke-local space) when it has some.
                // Measured on the user's PDFs: a piece without ranges whose mask holds none of its points is not shown.
                for (auto const& piece : pieces)
                {
                    auto const& ink = InkAt(inks, piece.Varint(4).value_or(record.Varint(4).value_or(0)));
                    auto pieceTransform = piece.Child(7);
                    auto transform = Transform(pieceTransform ? pieceTransform : record.Child(7));
                    auto ranges = piece.Floats(12);
                    std::vector<std::vector<RawPoint>> parts;
                    if (ranges.empty()) parts.push_back(points);
                    for (size_t i = 0; i + 1 < ranges.size(); i += 2) parts.push_back(Cut(points, ranges[i], ranges[i + 1]));
                    std::vector<std::vector<Point>> masks;
                    for (auto bytes : piece.AllBytes(10))
                    {
                        auto polygon = Polygon(bytes);
                        if (polygon.size() >= 3) masks.push_back(std::move(polygon));
                    }
                    for (auto const& part : parts)
                    {
                        if (masks.empty())
                        {
                            strokes.push_back(Stroke(ink, part, transform));
                            continue;
                        }
                        for (auto const& clipped : Clip(part, masks)) strokes.push_back(Stroke(ink, clipped, transform));
                    }
                }
            }
            std::erase_if(strokes, [](PencilKitStroke const& s) { return s.points.empty(); });
            return strokes;
        }

        std::vector<PencilKitStroke> ReadLegacy(Message const& root)
        {
            auto container = root.Child(2);
            auto drawing = container ? container->Child(3) : std::nullopt;
            if (!drawing) ThrowInvalidData("PencilKit drawing: no drawing in the data");
            auto inks = Inks(*drawing);
            std::vector<PencilKitStroke> strokes;
            for (auto const& record : drawing->Children(5))
            {
                auto count = record.Varint(4).value_or(0);
                auto data = record.Bytes(5).value_or(std::string_view{});
                if (count > data.size() || data.size() != count * 24)
                {
                    ThrowInvalidData(std::format("PencilKit drawing: {} points in {} bytes", count, data.size()));
                }
                std::vector<RawPoint> points;
                for (size_t i = 0; i < count; ++i)
                {
                    points.push_back({ F32(data, i * 24 + 4), F32(data, i * 24 + 8), U16(data, i * 24 + 12) / 10.0, 1 });
                }
                strokes.push_back(Stroke(InkAt(inks, record.Varint(3).value_or(0)), points, std::nullopt));
            }
            std::erase_if(strokes, [](PencilKitStroke const& s) { return s.points.empty(); });
            return strokes;
        }
    }

    std::vector<PencilKitStroke> PencilKitDrawing::Read(std::span<std::uint8_t const> data)
    {
        try
        {
            if (data.size() >= 6 && data[0] == 'w' && data[1] == 'r' && data[2] == 'd')
            {
                std::string_view body(reinterpret_cast<char const*>(data.data()) + 6, data.size() - 6);
                return ReadWrd(Message::Parse(body));
            }
            if (io::Gzip::IsGzip(data))
            {
                auto unpacked = io::Gzip::Decompress(data);
                std::string_view body(reinterpret_cast<char const*>(unpacked.data()), unpacked.size());
                return ReadLegacy(Message::Parse(body)); // the views into `unpacked` end with this call
            }
        }
        catch (protozero::exception const& e)
        {
            ThrowInvalidData(std::format("PencilKit drawing: damaged protobuf data ({})", e.what()));
        }
        ThrowInvalidData("not a PencilKit drawing");
    }

    // ---- writing (the older variant)

    namespace
    {
        template <class T>
        void Put(std::string& s, T value)
        {
            char bytes[sizeof(T)];
            std::memcpy(bytes, &value, sizeof(T));
            s.append(bytes, sizeof(T));
        }

        std::string Message(std::function<void(protozero::pbf_writer&)> const& write)
        {
            std::string buffer;
            {
                protozero::pbf_writer w(buffer);
                write(w);
            }
            return buffer;
        }

        // The three-number records that order the drawing's parts: {a, b, c} (varints 1-3).
        std::string Triple(std::uint32_t a, std::uint32_t b, std::uint32_t c)
        {
            return Message([&](protozero::pbf_writer& w) { w.add_uint32(1, a); w.add_uint32(2, b); w.add_uint32(3, c); });
        }

        // Identifiers: 16 random bytes (a UUID's size) - the drawing's, each stroke's two.
        std::string RandomId(std::mt19937_64& random)
        {
            std::string id(16, '\0');
            for (auto& c : id) c = static_cast<char>(random() & 0xFF);
            return id;
        }

        struct WrittenInk
        {
            std::string name;
            Color color;
            bool operator==(WrittenInk const&) const = default;
        };

        // iPadOS' pen points as the user's drawings have them (azimuth and altitude as the pen's usual tilt there).
        constexpr std::uint16_t Aspect = 1000, Force = 1000, Azimuth = 36000, Altitude = 65146;
        constexpr double PointInterval = 0.008; // seconds between two points (as a 120 Hz pen)
    }

    Rect PencilKitDrawing::BoundsOf(PencilKitStroke const& stroke)
    {
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30, widest = stroke.width;
        for (size_t k = 0; k < stroke.points.size(); ++k)
        {
            auto const& p = stroke.points[k];
            if (k < stroke.widths.size()) widest = std::max(widest, stroke.widths[k]);
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        double pad = widest / 2;
        double left = std::floor(minX - pad), top = std::floor(minY - pad);
        return { left, top, std::ceil(maxX + pad) - left, std::ceil(maxY + pad) - top };
    }

    std::vector<std::uint8_t> PencilKitDrawing::WriteLegacy(std::span<PencilKitStroke const> strokes)
    {
        if (strokes.empty()) ThrowInvalidData("PencilKit drawing: no strokes to write");
        std::mt19937_64 random{ std::random_device{}() };
        std::vector<WrittenInk> inks;
        std::string body = Message([&](protozero::pbf_writer& drawing)
        {
            drawing.add_uint32(1, 10);
            drawing.add_bytes(2, RandomId(random));
            drawing.add_bytes(2, std::string(16, '\0'));
            auto count = static_cast<std::uint32_t>(strokes.size());
            drawing.add_bytes(3, Triple(count, 0, count));
            drawing.add_bytes(3, Triple(0, 1, 0));
            // The inks, in the order the strokes first use them.
            std::vector<size_t> inkOf;
            for (auto const& stroke : strokes)
            {
                WrittenInk ink{ stroke.ink.empty() ? "com.apple.ink.pen" : stroke.ink, stroke.color };
                auto found = std::ranges::find(inks, ink);
                inkOf.push_back(static_cast<size_t>(found - inks.begin()));
                if (found == inks.end()) inks.push_back(ink);
            }
            for (auto const& ink : inks)
            {
                drawing.add_bytes(4, Message([&](protozero::pbf_writer& o)
                {
                    o.add_bytes(1, Message([&](protozero::pbf_writer& c)
                    {
                        c.add_float(1, ink.color.r / 255.0f);
                        c.add_float(2, ink.color.g / 255.0f);
                        c.add_float(3, ink.color.b / 255.0f);
                        c.add_float(4, ink.color.a / 255.0f);
                    }));
                    o.add_string(2, ink.name);
                    o.add_uint32(3, 3);
                }));
            }
            double now = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()) - 978307200.0; // since 2001-01-01 (Apple's epoch)
            double startsAt = now; // a stroke starts after the one before it ended (as iPadOS' times run)
            for (size_t i = 0; i < strokes.size(); ++i)
            {
                auto const& stroke = strokes[i];
                std::string points;
                for (size_t k = 0; k < stroke.points.size(); ++k)
                {
                    auto const& p = stroke.points[k];
                    double size = k < stroke.widths.size() ? stroke.widths[k] : stroke.width;
                    Put(points, static_cast<float>(PointInterval * static_cast<double>(k)));
                    Put(points, static_cast<float>(p.x));
                    Put(points, static_cast<float>(p.y));
                    Put(points, static_cast<std::uint16_t>(std::clamp(std::lround(size * 10), 1L, 65535L)));
                    Put(points, Aspect);
                    Put(points, std::uint16_t{ 0 });
                    Put(points, Force);
                    Put(points, Azimuth);
                    Put(points, Altitude);
                }
                Rect bounds = BoundsOf(stroke);
                double thisStart = startsAt;
                startsAt += PointInterval * static_cast<double>(stroke.points.size()) + 0.2;
                drawing.add_bytes(5, Message([&](protozero::pbf_writer& s)
                {
                    s.add_bytes(1, Triple(static_cast<std::uint32_t>(i), 0, static_cast<std::uint32_t>(i + 1)));
                    s.add_bytes(2, Triple(0, 0, 0));
                    s.add_uint32(3, static_cast<std::uint32_t>(inkOf[i]));
                    s.add_uint32(4, static_cast<std::uint32_t>(stroke.points.size()));
                    s.add_bytes(5, points);
                    s.add_bytes(6, Message([&](protozero::pbf_writer& b)
                    {
                        b.add_float(1, static_cast<float>(bounds.x));
                        b.add_float(2, static_cast<float>(bounds.y));
                        b.add_float(3, static_cast<float>(bounds.width));
                        b.add_float(4, static_cast<float>(bounds.height));
                    }));
                    s.add_uint32(9, 0);
                    s.add_double(11, thisStart);
                    s.add_bytes(13, RandomId(random));
                    s.add_bytes(14, RandomId(random));
                }));
            }
            drawing.add_bytes(9, RandomId(random));
            drawing.add_bytes(10, Triple(0, 1, 0));
        });
        std::string root = Message([&](protozero::pbf_writer& r)
        {
            r.add_uint32(1, 0);
            r.add_bytes(2, Message([&](protozero::pbf_writer& c)
            {
                c.add_uint32(1, 10);
                c.add_uint32(2, 10);
                c.add_bytes(3, body);
            }));
        });
        return io::Gzip::Compress({ reinterpret_cast<std::uint8_t const*>(root.data()), root.size() });
    }
}
