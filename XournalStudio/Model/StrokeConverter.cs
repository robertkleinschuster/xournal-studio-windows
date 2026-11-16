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
        var points = ReadPoints(e.Value).Points;
        int count = points.Count;
        if (count == 0)
        {
            return null;
        }
        var widths = ParseNumbers((string?)e.Attribute("width") ?? "1");
        // No positive width: a filled or styled stroke has no line (upstream: only its filling, if any); any other is
        // drawn 1 wide, as the ink (FromXml) – also a damaged one, as it was shown before. Missing: 1.
        double width = widths.Count == 0 ? 1 : widths[0] > 0 ? widths[0] : IsStyled(e) ? 0 : 1;
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

    // A dash pattern needs lengths of 0 or more, not all 0 (cairo, XAML and PDF draw no other), and a gap: the pattern
    // repeats (an odd one with on and off swapped), so without a positive length in an "off" place it is solid. Zero
    // "on" lengths are kept: upstream draws them as dots with round ends. One rule for the screen and the export.
    private static double[] Drawable(List<double> dashes)
    {
        var cycle = dashes.Count % 2 == 1 ? dashes.Concat(dashes).ToList() : dashes;
        bool drawable = dashes.All(d => d >= 0) && dashes.Any(d => d > 0) &&
            cycle.Where((_, k) => k % 2 == 1).Any(gap => gap > 0);
        return drawable ? dashes.ToArray() : [];
    }

    /// <summary>
    /// A stroke shown (and exported) from the file, not edited as ink, so the file keeps it as it is: one Windows ink
    /// cannot draw as the file means it (filled, with a line style, with other than round ends) or a damaged one (a
    /// number this app cannot read).
    /// </summary>
    public static bool NeedsOwnDrawing(XElement e) => IsStyled(e) || IsDamaged(e);

    // Filled, with a line style or with other than round ends: what Windows ink cannot draw.
    private static bool IsStyled(XElement e) =>
        (int.TryParse((string?)e.Attribute("fill"), NumberStyles.Integer, Inv, out int fill) && fill >= 0) ||
        IsLineStyle((string?)e.Attribute("style")) ||
        (string?)e.Attribute("capStyle") is "butt" or "square";

    // A number of the stroke this app cannot read (not a number, beyond any page, a stray coordinate): the stroke is
    // shown as far as it can be read and never rewritten as ink or moved, so nothing of it is lost or misplaced.
    private static bool IsDamaged(XElement e) =>
        ReadPoints(e.Value).After.Count > 0 ||
        ((string?)e.Attribute("width") ?? "1").Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Length !=
            ParseNumbers((string?)e.Attribute("width") ?? "1").Count;

    // Any line style (also one this app or upstream cannot draw dashed): such a stroke is never rewritten as ink,
    // which would drop the attribute. Solid strokes carry none ("plain" is upstream's name for solid).
    private static bool IsLineStyle(string? style) => style is { Length: > 0 } and not "plain";

    public static InkStroke? FromXml(XElement e)
    {
        var coords = ReadPoints(e.Value).Points;
        int count = coords.Count;
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
            points.Add(new InkPoint(coords[i], pressure));
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

    /// <param name="committed">The stroke's transform as written (a move previewed on it is not); null: its PointTransform.</param>
    public static XElement ToXml(InkStroke stroke, Matrix3x2? committed = null)
    {
        var attributes = stroke.DrawingAttributes;
        var transform = committed ?? stroke.PointTransform;
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
        // Exactly the points shown and drawn as ink are moved (in double: the file's numbers keep their precision,
        // Vector2 would round them to float); what follows them (a damaged number and everything after it, a stray
        // last number) is written back exactly as read – never deleted.
        var (points, after) = ReadPoints(e.Value);
        var moved = new List<string>(points.Count * 2 + after.Count);
        foreach (var p in points)
        {
            moved.Add(Format(p.X * m.M11 + p.Y * m.M21 + m.M31));
            moved.Add(Format(p.X * m.M12 + p.Y * m.M22 + m.M32));
        }
        moved.AddRange(after);
        e.Value = string.Join(' ', moved);
    }

    private static string Format(double v) => v.ToString("0.####", Inv);

    /// <summary>
    /// A stroke's points: its coordinate pairs by the file's one rule (XoppFormat.ReadNumber), up to the first damaged
    /// number – so point i keeps the width of segment i. The second part is what follows, as read. The one reader
    /// for display, ink, bounds, export and <see cref="Transform"/>.
    /// </summary>
    private static (List<Point> Points, List<string> After) ReadPoints(string s)
    {
        var tokens = s.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var points = new List<Point>(tokens.Length / 2);
        int i = 0;
        for (; i + 1 < tokens.Length; i += 2)
        {
            if (XoppFormat.ReadNumber(tokens[i]) is not { } x || XoppFormat.ReadNumber(tokens[i + 1]) is not { } y)
            {
                break;
            }
            points.Add(new Point(x, y));
        }
        return (points, tokens.Skip(i).ToList());
    }

    // Widths and dashes by the file's one rule (XoppFormat.ReadNumber); the list ends at the first damaged one.
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

/// <summary>A part of a drawn stroke (<see cref="StrokeShape.Parts"/>), in page points.</summary>
public abstract record StrokePart;

/// <summary>The filled outline.</summary>
public sealed record StrokeFill(IReadOnlyList<Point> Points) : StrokePart;

/// <summary>A one-point stroke: a circle, or a square for square ends, of the stroke's width.</summary>
public sealed record StrokeDot(Point Center, double Size, bool Square) : StrokePart;

/// <summary>A polyline of one width; <paramref name="DashOffset"/> = the length drawn before it (in points).</summary>
public sealed record StrokeLine(IReadOnlyList<Point> Points, double Width, double DashOffset) : StrokePart;

/// <summary>
/// A stroke as Xournal draws it (see <see cref="StrokeConverter.ReadShape"/>): <paramref name="Fill"/> is the alpha
/// (0–255) of the filled outline, -1 for none; <paramref name="Dashes"/> in page points, empty for solid.
/// </summary>
public sealed record StrokeShape(Color Color, bool Highlighter, double Width, IReadOnlyList<Point> Points,
    IReadOnlyList<double> SegmentWidths, int Fill = -1, double[]? Dashes = null, StrokeCap Cap = StrokeCap.Round)
{
    /// <summary>True when all segments have the stroke's width (no pressure): drawn as one polyline.</summary>
    public bool EvenWidth => SegmentWidths.All(w => w == Width);

    /// <summary>
    /// What is drawn of the stroke, bottom to top – the one description the screen (PageView) and the export
    /// (PdfExport) both draw: the filling (three points or more), then a dot (one point: its end shape, nothing for
    /// butt ends) or the line (one polyline; with pressure one per segment of positive width, the dashes running on).
    /// </summary>
    public IReadOnlyList<StrokePart> Parts()
    {
        var parts = new List<StrokePart>();
        if (Fill >= 0 && Points.Count > 2)
        {
            parts.Add(new StrokeFill(Points));
        }
        if (Points.Count == 1)
        {
            if (Width > 0 && Cap != StrokeCap.Butt)
            {
                parts.Add(new StrokeDot(Points[0], Width, Cap == StrokeCap.Square));
            }
        }
        else if (EvenWidth)
        {
            if (Width > 0)
            {
                parts.Add(new StrokeLine(Points, Width, 0));
            }
        }
        else
        {
            double drawn = 0;
            for (int i = 0; i + 1 < Points.Count; i++)
            {
                var (a, b) = (Points[i], Points[i + 1]);
                if (SegmentWidths[i] > 0)
                {
                    parts.Add(new StrokeLine([a, b], SegmentWidths[i], drawn));
                }
                drawn += Math.Sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y));
            }
        }
        return parts;
    }

    /// <summary>
    /// The lowest point of what is drawn (<see cref="Parts"/>; at most this low for square-ended dashes), or null when
    /// nothing is.
    /// </summary>
    public double? Bottom()
    {
        // A line reaches half its width below its points (round ends and joins too); a square end reaches half the width
        // beyond the end, also sideways: at most √2 · width / 2 below it – at the line's two ends, or (an upper bound) at
        // any point of a dashed line, where a dash may end. Gaps of dashes are counted as drawn.
        double Ends(StrokeLine l)
        {
            if (Cap != StrokeCap.Square)
            {
                return double.NegativeInfinity;
            }
            double lowest = DashesFor(l.Width) != null ? l.Points.Max(p => p.Y) : Math.Max(l.Points[0].Y, l.Points[^1].Y);
            return lowest + Math.Sqrt(2) * l.Width / 2;
        }
        var bottoms = Parts().Select(part => part switch
        {
            StrokeFill f => f.Points.Max(p => p.Y),
            StrokeDot d => d.Center.Y + d.Size / 2,
            StrokeLine l => Math.Max(l.Points.Max(p => p.Y) + l.Width / 2, Ends(l)),
            _ => throw new InvalidOperationException(part.GetType().Name),
        }).ToList();
        return bottoms.Count > 0 ? bottoms.Max() : null;
    }

    /// <summary>
    /// The dash pattern for a line of <paramref name="width"/> in multiples of it (as XAML and PDF pens take it), or
    /// null for a solid line – one rule for the screen and the export.
    /// </summary>
    public double[]? DashesFor(double width)
    {
        if (Dashes is not { Length: > 0 } dashes || width <= 0 || !double.IsFinite(dashes.Max() / width))
        {
            return null;
        }
        return dashes.Select(d => d / width).ToArray();
    }
}
