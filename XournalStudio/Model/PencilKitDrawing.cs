using System.Buffers.Binary;
using System.IO.Compression;
using Google.Protobuf;
using Windows.Foundation;

namespace XournalStudio.Model;

/// <summary>A visible stroke of a PencilKit drawing: drawing coordinates (points, y down), PencilKit's own width.</summary>
/// <param name="Ink">The ink's identifier, e.g. <c>com.apple.ink.pen</c>, <c>com.apple.ink.marker</c>.</param>
/// <param name="Color">The ink's color, its alpha lowered by the points' opacity (pencil).</param>
/// <param name="Width">The points' median width (PencilKit's brush size), scaled by the stroke's transform.</param>
public sealed record PencilKitStroke(string Ink, Windows.UI.Color Color, IReadOnlyList<Point> Points, double Width)
{
    public bool IsMarker => Ink.EndsWith(".marker", StringComparison.OrdinalIgnoreCase);
}

/// <summary>
/// Reads Apple's PencilKit drawings (<c>PKDrawing.dataRepresentation</c>), the ink of iPad markup in PDFs. Apple does not
/// document the format: the layout follows two decoders validated against Apple's framework – libfreeform's
/// <c>src/pkdrawing.rs</c> (Apache-2.0) and notability-goodnotes' <c>gnnote/pencilkit.py</c> (MIT) – and was checked
/// against the appearance images of the user's own iPad PDFs (2026-11-18). Two variants occur:
/// <list type="bullet">
/// <item><c>wrd\xf0</c> + version (u16), then a protobuf message: inks (#4: color #1 {r,g,b,a}, identifier #2) and strokes
/// (#5: ink #4, path #5, transform #7, visible pieces #11 of an erased stroke: ink #4, transform #7, mask polygons #10,
/// parametric ranges #12); a path has the point count (#3),
/// the masks of per-point (#4) and constant (#5) channels, the constant (#6) and per-point (#7) records.</item>
/// <item>older drawings: gzip, then a protobuf message whose #2.#3 holds inks (#4) and strokes (#5: ink #3, point count
/// #4, 24-byte points #5: time, x, y as f32, then width × 10, aspect, edge, force, azimuth, altitude as u16).</item>
/// </list>
/// The stored points are the curve's control points; they become the stroke's polyline (they lie a point or two apart).
/// Pressure is not taken over: the user's drawings have force 1 everywhere.
/// </summary>
public static class PencilKitDrawing
{
    // Channel sizes in bit order (PKStrokePathPointsShared): location (2 f32), time (f32), width (f32), aspect, edge
    // width, force, azimuth, altitude, opacity (u16 each), secondary radius (f32), threshold (u16), elevation (f32).
    private static readonly int[] ChannelSizes = [8, 4, 4, 2, 2, 2, 2, 2, 2, 4, 2, 4];
    private const int Location = 0, Width = 2, Opacity = 8;

    /// <summary>The visible strokes in drawing order. Throws <see cref="FormatException"/> for data it cannot read.</summary>
    public static IReadOnlyList<PencilKitStroke> Read(byte[] data)
    {
        try
        {
            if (data.Length >= 6 && data[0] == 'w' && data[1] == 'r' && data[2] == 'd')
            {
                return ReadWrd(Message.Parse(data.AsSpan(6).ToArray()));
            }
            if (data.Length >= 2 && data[0] == 0x1f && data[1] == 0x8b)
            {
                return ReadLegacy(Message.Parse(Gunzip(data)));
            }
            throw new FormatException("not a PencilKit drawing");
        }
        catch (InvalidProtocolBufferException e)
        {
            throw new FormatException($"damaged PencilKit drawing: {e.Message}", e);
        }
        catch (Exception e) when (e is InvalidDataException or OverflowException or ArgumentException or IndexOutOfRangeException)
        {
            // gzip, a count beyond int, a record cut short: damaged data, not a bug here.
            throw new FormatException($"damaged PencilKit drawing: {e.GetType().Name}: {e.Message}", e);
        }
    }

    private static byte[] Gunzip(byte[] data)
    {
        using var gzip = new GZipStream(new MemoryStream(data), CompressionMode.Decompress);
        using var result = new MemoryStream();
        gzip.CopyTo(result);
        return result.ToArray();
    }

    private sealed record Ink(string Identifier, Windows.UI.Color Color);

    private static List<Ink> Inks(Message drawing) => drawing.Children(4).Select(ink =>
    {
        var c = ink.Child(1);
        byte Channel(int n, float fallback) => (byte)Math.Round(Math.Clamp(c?.Float(n) ?? fallback, 0, 1) * 255);
        return new Ink(ink.String(2) ?? "", Windows.UI.Color.FromArgb(Channel(4, 1), Channel(1, 0), Channel(2, 0), Channel(3, 0)));
    }).ToList();

    private static Ink InkAt(List<Ink> inks, ulong index) =>
        index < (ulong)inks.Count ? inks[(int)index] : throw new FormatException($"stroke refers to ink {index} of {inks.Count}");

    private static List<PencilKitStroke> ReadWrd(Message drawing)
    {
        var inks = Inks(drawing);
        var strokes = new List<PencilKitStroke>();
        foreach (var record in drawing.Children(5))
        {
            if (record.Child(5) is not { } path)
            {
                continue; // a deleted stroke
            }
            var points = Points(path);
            var pieces = record.Children(11).ToList();
            if (pieces.Count == 0)
            {
                strokes.Add(Stroke(InkAt(inks, record.Varint(4) ?? 0), points, Transform(record.Child(7))));
                continue;
            }
            // Partly erased: each visible piece is a range of the path (parametric: point index plus fraction; all of it
            // when none is given), shown only inside its mask polygons (stroke-local space) when it has some. Measured on
            // the user's PDFs: a piece without ranges whose mask holds none of its points is not shown by Apple.
            foreach (var piece in pieces)
            {
                var ink = InkAt(inks, piece.Varint(4) ?? record.Varint(4) ?? 0);
                var transform = Transform(piece.Child(7) ?? record.Child(7));
                var ranges = piece.Floats(12).ToList();
                var parts = ranges.Count == 0
                    ? [points]
                    : Enumerable.Range(0, ranges.Count / 2).Select(i => Cut(points, ranges[2 * i], ranges[2 * i + 1])).ToList();
                var masks = piece.AllBytes(10).Select(Polygon).Where(p => p.Count >= 3).ToList();
                foreach (var part in masks.Count == 0 ? parts : parts.SelectMany(p => Clip(p, masks)))
                {
                    strokes.Add(Stroke(ink, part, transform));
                }
            }
        }
        return strokes.Where(s => s.Points.Count > 0).ToList();
    }

    private static List<PencilKitStroke> ReadLegacy(Message root)
    {
        var drawing = root.Child(2)?.Child(3) ?? throw new FormatException("no drawing in the data");
        var inks = Inks(drawing);
        var strokes = new List<PencilKitStroke>();
        foreach (var record in drawing.Children(5))
        {
            int count = checked((int)(record.Varint(4) ?? 0));
            byte[] data = record.Bytes(5) ?? [];
            if (data.Length != count * 24)
            {
                throw new FormatException($"{count} points in {data.Length} bytes");
            }
            var points = new List<RawPoint>(count);
            for (int i = 0; i < count; i++)
            {
                var p = data.AsSpan(i * 24);
                points.Add(new RawPoint(BinaryPrimitives.ReadSingleLittleEndian(p[4..]), BinaryPrimitives.ReadSingleLittleEndian(p[8..]),
                    BinaryPrimitives.ReadUInt16LittleEndian(p[12..]) / 10.0, 1));
            }
            strokes.Add(Stroke(InkAt(inks, record.Varint(3) ?? 0), points, null));
        }
        return strokes.Where(s => s.Points.Count > 0).ToList();
    }

    private readonly record struct RawPoint(double X, double Y, double Width, double Opacity);

    private static List<RawPoint> Points(Message path)
    {
        int count = checked((int)(path.Varint(3) ?? 0));
        ulong perPoint = path.Varint(4) ?? throw new FormatException("path without channel mask");
        ulong constant = path.Varint(5) ?? 0;
        if (((perPoint | constant) >> ChannelSizes.Length) != 0 || (perPoint & constant) != 0 || (perPoint & 1) == 0)
        {
            throw new FormatException($"unexpected channel masks {perPoint:x}/{constant:x}");
        }
        int stride = Size(perPoint);
        byte[] records = path.Bytes(7) ?? [];
        byte[] constants = path.Bytes(6) ?? [];
        if (records.Length != count * stride || constants.Length != Size(constant))
        {
            throw new FormatException("point records do not match their masks");
        }
        var shared = Channels(constants, constant);
        var points = new List<RawPoint>(count);
        for (int i = 0; i < count; i++)
        {
            var values = Channels(records.AsSpan(i * stride, stride), perPoint);
            double Value(int channel, double fallback) =>
                values.TryGetValue(channel, out var v) ? v : shared.TryGetValue(channel, out var s) ? s : fallback;
            points.Add(new RawPoint(values[-1], values[Location], Value(Width, 0), Value(Opacity, 1)));
        }
        return points;
    }

    private static int Size(ulong mask) => Enumerable.Range(0, ChannelSizes.Length).Where(i => (mask >> i & 1) != 0).Sum(i => ChannelSizes[i]);

    // The channels of a record (location: x as -1, y as 0), decoded as libfreeform does.
    private static Dictionary<int, double> Channels(ReadOnlySpan<byte> record, ulong mask)
    {
        var values = new Dictionary<int, double>();
        int offset = 0;
        for (int i = 0; i < ChannelSizes.Length; i++)
        {
            if ((mask >> i & 1) == 0)
            {
                continue;
            }
            var bytes = record[offset..];
            switch (i)
            {
                case Location:
                    values[-1] = BinaryPrimitives.ReadSingleLittleEndian(bytes);
                    values[0] = BinaryPrimitives.ReadSingleLittleEndian(bytes[4..]);
                    break;
                case 1 or 2 or 9 or 11:
                    values[i] = BinaryPrimitives.ReadSingleLittleEndian(bytes);
                    break;
                default:
                    double raw = BinaryPrimitives.ReadUInt16LittleEndian(bytes);
                    values[i] = i is Opacity or 10 ? raw / 32767.5 : raw / 1000;
                    break;
            }
            offset += ChannelSizes[i];
        }
        return values;
    }

    // The part of the path between two parameters (point index plus fraction), interpolated at both ends.
    private static List<RawPoint> Cut(List<RawPoint> points, double start, double end)
    {
        var cut = new List<RawPoint>();
        if (points.Count == 0 || end < start)
        {
            return cut;
        }
        RawPoint At(double t)
        {
            t = Math.Clamp(t, 0, points.Count - 1);
            int i = (int)Math.Floor(t);
            if (i >= points.Count - 1)
            {
                return points[^1];
            }
            double f = t - i;
            var (a, b) = (points[i], points[i + 1]);
            return new RawPoint(a.X + f * (b.X - a.X), a.Y + f * (b.Y - a.Y), a.Width + f * (b.Width - a.Width), a.Opacity);
        }
        cut.Add(At(start));
        for (int i = (int)Math.Floor(start) + 1; i < end && i < points.Count; i++)
        {
            cut.Add(points[i]);
        }
        cut.Add(At(end));
        return cut;
    }

    private static List<Point> Polygon(byte[] data) => Enumerable.Range(0, data.Length / 8)
        .Select(i => new Point(BinaryPrimitives.ReadSingleLittleEndian(data.AsSpan(i * 8)), BinaryPrimitives.ReadSingleLittleEndian(data.AsSpan(i * 8 + 4))))
        .ToList();

    // The runs of the polyline inside the polygons (even-odd), sampled every quarter point along it.
    private static IEnumerable<List<RawPoint>> Clip(List<RawPoint> points, List<List<Point>> polygons)
    {
        const double Step = 0.25;
        var run = new List<RawPoint>();
        bool Inside(RawPoint p) => polygons.Count(polygon => Contains(polygon, p.X, p.Y)) % 2 == 1;
        IEnumerable<RawPoint> Samples()
        {
            for (int i = 0; i < points.Count; i++)
            {
                if (i > 0)
                {
                    var (a, b) = (points[i - 1], points[i]);
                    int n = (int)Math.Ceiling(Math.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y)) / Step);
                    for (int k = 1; k < n; k++)
                    {
                        double f = (double)k / n;
                        yield return new RawPoint(a.X + f * (b.X - a.X), a.Y + f * (b.Y - a.Y), a.Width, a.Opacity);
                    }
                }
                yield return points[i];
            }
        }
        foreach (var sample in Samples())
        {
            if (Inside(sample))
            {
                run.Add(sample);
            }
            else if (run.Count > 0)
            {
                yield return Thinned(run);
                run = [];
            }
        }
        if (run.Count > 0)
        {
            yield return Thinned(run);
        }
    }

    // The samples a point apart (as dense as the drawing's own points), with both ends.
    private static List<RawPoint> Thinned(List<RawPoint> run)
    {
        var kept = new List<RawPoint> { run[0] };
        for (int i = 1; i < run.Count; i++)
        {
            var last = kept[^1];
            if (i == run.Count - 1 || Math.Abs(run[i].X - last.X) + Math.Abs(run[i].Y - last.Y) >= 1)
            {
                kept.Add(run[i]);
            }
        }
        return kept;
    }

    private static bool Contains(List<Point> polygon, double x, double y)
    {
        bool inside = false;
        for (int i = 0, j = polygon.Count - 1; i < polygon.Count; j = i++)
        {
            var (a, b) = (polygon[i], polygon[j]);
            if ((a.Y > y) != (b.Y > y) && x < (b.X - a.X) * (y - a.Y) / (b.Y - a.Y) + a.X)
            {
                inside = !inside;
            }
        }
        return inside;
    }

    private static System.Numerics.Matrix3x2? Transform(Message? m) => m == null ? null : new System.Numerics.Matrix3x2(
        m.Float(1) ?? 1, m.Float(2) ?? 0, m.Float(3) ?? 0, m.Float(4) ?? 1, m.Float(5) ?? 0, m.Float(6) ?? 0);

    private static PencilKitStroke Stroke(Ink ink, List<RawPoint> raw, System.Numerics.Matrix3x2? transform)
    {
        var m = transform ?? System.Numerics.Matrix3x2.Identity;
        var points = raw.Select(p => System.Numerics.Vector2.Transform(new((float)p.X, (float)p.Y), m))
            .Where(v => float.IsFinite(v.X) && float.IsFinite(v.Y))
            .Select(v => new Point(v.X, v.Y)).ToList();
        double scale = Math.Sqrt(Math.Abs(m.GetDeterminant()));
        double width = Median(raw.Select(p => p.Width).Where(w => w > 0 && double.IsFinite(w))) * scale;
        double opacity = Math.Clamp(Median(raw.Select(p => p.Opacity)), 0, 1);
        var color = ink.Color;
        if (opacity < 0.999)
        {
            color.A = (byte)Math.Round(color.A * opacity);
        }
        return new PencilKitStroke(ink.Identifier, color, points, width);
    }

    private static double Median(IEnumerable<double> values)
    {
        var sorted = values.Order().ToList();
        return sorted.Count == 0 ? 0 : sorted[sorted.Count / 2];
    }

    /// <summary>A protobuf message read field by field (the schema is not published), with Google.Protobuf's reader.</summary>
    private sealed class Message
    {
        private readonly List<(int Number, ulong Value, byte[]? Bytes)> fields = [];

        public static Message Parse(byte[] data)
        {
            var message = new Message();
            var input = new CodedInputStream(data);
            uint tag;
            while ((tag = input.ReadTag()) != 0)
            {
                int number = WireFormat.GetTagFieldNumber(tag);
                switch (WireFormat.GetTagWireType(tag))
                {
                    case WireFormat.WireType.Varint:
                        message.fields.Add((number, input.ReadUInt64(), null));
                        break;
                    case WireFormat.WireType.Fixed32:
                        message.fields.Add((number, input.ReadFixed32(), null));
                        break;
                    case WireFormat.WireType.Fixed64:
                        message.fields.Add((number, input.ReadFixed64(), null));
                        break;
                    case WireFormat.WireType.LengthDelimited:
                        message.fields.Add((number, 0, input.ReadBytes().ToByteArray()));
                        break;
                    default:
                        throw new FormatException($"unexpected wire type in field {number}");
                }
            }
            return message;
        }

        public ulong? Varint(int number) => fields.FirstOrDefault(f => f.Number == number && f.Bytes == null) is { Number: > 0 } f ? f.Value : null;

        public float? Float(int number) => Varint(number) is { } v ? BitConverter.UInt32BitsToSingle((uint)v) : null;

        // Repeated floats, each its own field or packed into one (protobuf's packed encoding).
        public IEnumerable<float> Floats(int number) => fields.Where(f => f.Number == number).SelectMany(f => f.Bytes == null
            ? [BitConverter.UInt32BitsToSingle((uint)f.Value)]
            : Enumerable.Range(0, f.Bytes.Length / 4).Select(i => BinaryPrimitives.ReadSingleLittleEndian(f.Bytes.AsSpan(i * 4))));

        public byte[]? Bytes(int number) => fields.FirstOrDefault(f => f.Number == number && f.Bytes != null).Bytes;

        public IEnumerable<byte[]> AllBytes(int number) => fields.Where(f => f.Number == number && f.Bytes != null).Select(f => f.Bytes!);

        public string? String(int number) => Bytes(number) is { } b ? System.Text.Encoding.UTF8.GetString(b) : null;

        public Message? Child(int number) => Bytes(number) is { } b ? Parse(b) : null;

        public IEnumerable<Message> Children(int number) =>
            fields.Where(f => f.Number == number && f.Bytes != null).Select(f => Parse(f.Bytes!));
    }
}
