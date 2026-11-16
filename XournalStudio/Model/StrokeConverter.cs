using System.Globalization;
using System.Numerics;
using System.Text;
using System.Xml.Linq;
using Windows.Foundation;
using Windows.UI;
using Windows.UI.Input.Inking;

namespace XournalStudio.Model;

/// <summary>
/// Converts between Xournal's &lt;stroke&gt; elements and Windows ink strokes.
/// Page coordinates are Xournal points (1/72"), used 1:1 as XAML DIPs of the page's InkCanvas.
/// </summary>
public static class StrokeConverter
{
    // Xournal stores per-segment widths (tool width * pressure). Windows ink draws a stroke of size s at
    // pressure p about s * (0.25 + 1.5 p) wide (measured), i.e. exactly s at p = 0.5.
    private static double WidthFactor(double pressure) => 0.25 + 1.5 * pressure;

    /// <summary>The width Windows ink draws a stroke with at a pressure (measured, see WidthFactor).</summary>
    public static double DrawnWidth(InkDrawingAttributes attributes, float pressure) =>
        attributes.IgnorePressure ? attributes.Size.Width : attributes.Size.Width * WidthFactor(pressure);

    /// <summary>A stroke through these samples with these attributes (what the partial eraser leaves of a stroke).</summary>
    public static InkStroke FromSamples(IEnumerable<InkSample> samples, InkDrawingAttributes attributes)
    {
        Builder.SetDefaultDrawingAttributes(attributes);
        var stroke = Builder.CreateStrokeFromInkPoints(samples.Select(s => new InkPoint(s.Position, s.Pressure)), Matrix3x2.Identity);
        stroke.DrawingAttributes = attributes;
        return stroke;
    }

    private static double PressureFor(double widthFactor) => (widthFactor - 0.25) / 1.5;

    private const byte HighlighterAlpha = 0x7f;

    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    private static readonly InkStrokeBuilder Builder = new();

    /// <summary>
    /// A stroke element as drawn by Xournal: its color (with the file's alpha), its points and the width of each
    /// segment (the per-segment widths of a pressure stroke, otherwise the stroke's width). Null without points.
    /// </summary>
    public static StrokeShape? ReadShape(XElement e)
    {
        var coords = ParseNumbers(e.Value);
        int count = coords.Count / 2;
        if (count == 0)
        {
            return null;
        }
        var points = new List<Point>(count);
        for (int i = 0; i < count; i++)
        {
            points.Add(new Point(coords[2 * i], coords[2 * i + 1]));
        }
        var widths = ParseNumbers((string?)e.Attribute("width") ?? "1");
        double width = widths.Count > 0 && widths[0] > 0 ? widths[0] : 1;
        var segments = new List<double>(Math.Max(0, count - 1));
        for (int i = 0; i < count - 1; i++)
        {
            segments.Add(widths.Count > 1 ? widths[Math.Min(i + 1, widths.Count - 1)] : width);
        }
        bool highlighter = (string?)e.Attribute("tool") == "highlighter";
        int fill = int.TryParse((string?)e.Attribute("fill"), NumberStyles.Integer, Inv, out int f) ? f : -1;
        var cap = (string?)e.Attribute("capStyle") switch
        {
            "butt" => StrokeCap.Butt,
            "square" => StrokeCap.Square,
            _ => StrokeCap.Round,
        };
        return new StrokeShape(XoppColor.Parse((string?)e.Attribute("color"), Microsoft.UI.Colors.Black), highlighter,
            width, points, segments, fill, Dashes((string?)e.Attribute("style")), cap);
    }

    /// <summary>
    /// The dash pattern of the "style" attribute in page points (upstream StrokeStyle::parseStyle: "dash", "dashdot",
    /// "dot" or "cust: a b ..."); empty for a solid line.
    /// </summary>
    public static double[] Dashes(string? style) => style switch
    {
        "dash" => [6, 3],
        "dashdot" => [6, 3, 0.5, 3],
        "dot" => [0.5, 3],
        string s when s.StartsWith("cust: ", StringComparison.Ordinal) => Drawable(ParseNumbers(s[6..])),
        _ => [],
    };

    // A dash pattern needs positive lengths (negative or all-zero ones are invalid in XAML and PDF): otherwise solid.
    private static double[] Drawable(List<double> dashes) =>
        dashes.All(d => d >= 0) && dashes.Any(d => d > 0) ? dashes.ToArray() : [];

    /// <summary>
    /// A stroke Windows ink cannot draw as the file means it: filled, dashed, or with other than round ends. Such strokes
    /// are shown (and exported) from the file, not edited as ink; the file keeps them as they are.
    /// </summary>
    public static bool NeedsOwnDrawing(XElement e) =>
        (int.TryParse((string?)e.Attribute("fill"), NumberStyles.Integer, Inv, out int fill) && fill >= 0) ||
        Dashes((string?)e.Attribute("style")).Length > 0 ||
        (string?)e.Attribute("capStyle") is "butt" or "square";

    public static InkStroke? FromXml(XElement e)
    {
        var coords = ParseNumbers(e.Value);
        int count = coords.Count / 2;
        if (count == 0)
        {
            return null;
        }

        var widths = ParseNumbers((string?)e.Attribute("width") ?? "1");
        double width = widths.Count > 0 && widths[0] > 0 ? widths[0] : 1;
        bool hasPressure = widths.Count > 1;
        bool highlighter = (string?)e.Attribute("tool") == "highlighter";

        var color = XoppColor.Parse((string?)e.Attribute("color"), Microsoft.UI.Colors.Black);
        color.A = 255;

        var attributes = new InkDrawingAttributes
        {
            Color = color,
            Size = new Size(width, width),
            PenTip = PenTipShape.Circle,
            // Xournal++ draws the points as a polyline; Bezier fitting rounds the corners of shapes (rectangles).
            FitToCurve = false,
            IgnorePressure = !hasPressure,
            DrawAsHighlighter = highlighter,
        };

        var points = new List<InkPoint>(count + 1);
        for (int i = 0; i < count; i++)
        {
            float pressure = 0.5f;
            if (hasPressure)
            {
                // widths[i + 1] is the width of segment i; the last point has none.
                double w = widths[Math.Min(i + 1, widths.Count - 1)];
                pressure = (float)Math.Clamp(PressureFor(w / width), 0.01, 1.0);
            }
            points.Add(new InkPoint(new Point(coords[2 * i], coords[2 * i + 1]), pressure));
        }
        if (count == 1)
        {
            // A dot: ink needs at least two points.
            points.Add(points[0]);
        }

        Builder.SetDefaultDrawingAttributes(attributes);
        var stroke = Builder.CreateStrokeFromInkPoints(points, Matrix3x2.Identity);
        stroke.DrawingAttributes = attributes;
        return stroke;
    }

    public static XElement ToXml(InkStroke stroke)
    {
        var attributes = stroke.DrawingAttributes;
        var transform = stroke.PointTransform;
        var points = stroke.GetInkPoints();
        double width = attributes.Size.Width;

        var coords = new StringBuilder(points.Count * 16);
        foreach (var p in points)
        {
            var pos = Vector2.Transform(new Vector2((float)p.Position.X, (float)p.Position.Y), transform);
            if (coords.Length > 0)
            {
                coords.Append(' ');
            }
            coords.Append(Format(pos.X)).Append(' ').Append(Format(pos.Y));
        }

        var widthValue = new StringBuilder(Format(width));
        if (!attributes.IgnorePressure)
        {
            for (int i = 0; i < points.Count - 1; i++)
            {
                widthValue.Append(' ').Append(Format(width * WidthFactor(points[i].Pressure)));
            }
        }

        bool highlighter = attributes.DrawAsHighlighter;
        var color = attributes.Color;
        color.A = highlighter ? HighlighterAlpha : (byte)255;

        return new XElement("stroke",
            new XAttribute("tool", highlighter ? "highlighter" : "pen"),
            new XAttribute("color", XoppColor.Format(color)),
            new XAttribute("width", widthValue.ToString()),
            coords.ToString());
    }

    /// <summary>
    /// Moves/scales the points of a stroke element (as Windows ink's <c>PointTransform</c> does: the widths stay); its
    /// attributes, also unknown ones, stay as they are.
    /// </summary>
    public static void Transform(XElement e, Matrix3x2 m)
    {
        var tokens = e.Value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var coords = ParseNumbers(e.Value); // the readable pairs at the start
        var moved = new StringBuilder(e.Value.Length + 16);
        int pairs = coords.Count / 2;
        for (int i = 0; i < pairs; i++)
        {
            if (i > 0)
            {
                moved.Append(' ');
            }
            // In double: the file's numbers keep their precision (Vector2 would round them to float).
            double x = coords[2 * i] * m.M11 + coords[2 * i + 1] * m.M21 + m.M31;
            double y = coords[2 * i] * m.M12 + coords[2 * i + 1] * m.M22 + m.M32;
            moved.Append(Format(x)).Append(' ').Append(Format(y));
        }
        // What is not a readable pair (a stray number, a damaged rest) is written back exactly as read: never deleted.
        foreach (var rest in tokens.Skip(2 * pairs))
        {
            moved.Append(moved.Length > 0 ? " " : "").Append(rest);
        }
        e.Value = moved.ToString();
    }

    private static string Format(double v) => v.ToString("0.####", Inv);

    // Numbers of a stroke (coordinates, widths, dashes) by the file's one rule (XoppFormat.ReadNumber), for showing
    // and reading as ink; the list ends at the first one that is not a number or beyond any page, so the coordinates
    // stay in pairs. The element itself keeps everything (Transform writes the rest back as read).
    private static List<double> ParseNumbers(string s)
    {
        var result = new List<double>();
        foreach (var part in s.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
        {
            if (XoppFormat.ReadNumber(part) is not { } v)
            {
                break;
            }
            result.Add(v);
        }
        return result;
    }
}

public static class XoppColor
{
    /// <summary>Parses "#rrggbbaa" (Xournal) or a few named colors of old Xournal files.</summary>
    public static Color Parse(string? s, Color fallback)
    {
        if (string.IsNullOrEmpty(s))
        {
            return fallback;
        }
        if (s[0] == '#' && (s.Length == 9 || s.Length == 7) &&
            uint.TryParse(s.AsSpan(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v))
        {
            if (s.Length == 7)
            {
                v = (v << 8) | 0xff;
            }
            return Color.FromArgb((byte)v, (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8));
        }
        return s switch
        {
            "black" => Color.FromArgb(255, 0, 0, 0),
            "blue" => Color.FromArgb(255, 0x33, 0x33, 0xcc),
            "red" => Color.FromArgb(255, 0xff, 0, 0),
            "green" => Color.FromArgb(255, 0, 0x80, 0),
            "gray" => Color.FromArgb(255, 0x80, 0x80, 0x80),
            "lightblue" => Color.FromArgb(255, 0, 0xc0, 0xff),
            "lightgreen" => Color.FromArgb(255, 0, 0xff, 0),
            "magenta" => Color.FromArgb(255, 0xff, 0, 0xff),
            "orange" => Color.FromArgb(255, 0xff, 0x80, 0),
            "yellow" => Color.FromArgb(255, 0xff, 0xff, 0),
            "white" => Color.FromArgb(255, 0xff, 0xff, 0xff),
            _ => fallback,
        };
    }

    public static string Format(Color c) => $"#{c.R:x2}{c.G:x2}{c.B:x2}{c.A:x2}";
}

public enum StrokeCap { Round, Butt, Square }

/// <summary>
/// A stroke as Xournal draws it (see <see cref="StrokeConverter.ReadShape"/>): <paramref name="Fill"/> is the alpha
/// (0–255) of the filled outline, -1 for none; <paramref name="Dashes"/> in page points, empty for solid.
/// </summary>
public sealed record StrokeShape(Color Color, bool Highlighter, double Width, IReadOnlyList<Point> Points,
    IReadOnlyList<double> SegmentWidths, int Fill = -1, double[]? Dashes = null, StrokeCap Cap = StrokeCap.Round)
{
    /// <summary>True when all segments have the stroke's width (no pressure): drawn as one polyline.</summary>
    public bool EvenWidth => SegmentWidths.All(w => w == Width);
}
