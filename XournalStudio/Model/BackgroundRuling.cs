using System.Globalization;
using Windows.Foundation;
using Windows.UI;

namespace XournalStudio.Model;

/// <summary>Lines (and dots, drawn as filled circles of <see cref="Width"/>) of one color and width, in page points.</summary>
public sealed record RulingLines(Color Color, double Width, IReadOnlyList<(Point From, Point To)> Lines, IReadOnlyList<Point> Dots);

/// <summary>
/// The rulings of solid page backgrounds, ported from upstream Xournal++'s background views
/// (src/core/view/background: Ruled, Lined, Staves, Graph, Dotted, IsoDotted, IsoGraph) with their defaults and the
/// background's "config" attribute (r1 raster, m1 margin, rm round margin, lw line width, bli/blw bold lines, f1/f2
/// colors, af1/af2 colors on dark paper). Shown on screen and exported.
/// </summary>
public static class BackgroundRuling
{
    private const double HeaderSize = 80, FooterSize = 60;

    private static readonly Color Silver = Rgb(0xbdbdbd), DarkSlateGray = Rgb(0x434343), DodgerBlue = Rgb(0x40a0ff),
        DeepPink = Rgb(0xff0080), MidnightBlue = Rgb(0x220080), Black = Rgb(0x000000), White = Rgb(0xffffff);

    /// <summary>The rulings of a page's background, bottom to top; empty for plain paper.</summary>
    public static IReadOnlyList<RulingLines> For(BackgroundStyle style, string? config, Color paper, double width, double height)
    {
        var c = Parse(config);
        bool light = paper.R + paper.G + paper.B > 0x180; // upstream Color::isLight
        Color Foreground(string key, string altKey, Color normal, Color alternative) =>
            light ? HexColor(c, key) ?? normal : HexColor(c, altKey) ?? alternative;
        double lw(double fallback) => Number(c, "lw") ?? fallback;

        switch (style)
        {
            case BackgroundStyle.Ruled:
                return [Ruled(c, Foreground("f1", "af1", DodgerBlue, DarkSlateGray), lw(0.5), width, height)];
            case BackgroundStyle.Lined:
            {
                double lineWidth = lw(0.5);
                double margin = Number(c, "m1") ?? 72;
                if (margin < 0)
                {
                    margin += width; // a negative margin puts the line on the right
                }
                return
                [
                    Ruled(c, Foreground("f1", "af1", DodgerBlue, DarkSlateGray), lineWidth, width, height),
                    new RulingLines(Foreground("f2", "af2", DeepPink, MidnightBlue), lineWidth,
                        [(new Point(margin, 0), new Point(margin, height))], []),
                ];
            }
            case BackgroundStyle.Staves:
                return [Staves(Foreground("f1", "af1", Black, White), lw(0.5), width, height)];
            case BackgroundStyle.Graph:
                return Graph(c, Foreground("f1", "af1", Silver, DarkSlateGray), lw(0.5), width, height);
            case BackgroundStyle.Dotted:
                return [Dotted(c, Foreground("f1", "af1", Silver, DarkSlateGray), lw(1.5), width, height)];
            case BackgroundStyle.IsoDotted:
                return [Isometric(c, Foreground("f1", "af1", Silver, DarkSlateGray), lw(1.5), width, height, dots: true)];
            case BackgroundStyle.IsoGraph:
                return [Isometric(c, Foreground("f1", "af1", Silver, DarkSlateGray), lw(1.0), width, height, dots: false)];
            default:
                return [];
        }
    }

    // Indices i with i * step in [margin, length - margin] (upstream getIndexBounds for the whole page).
    private static (int Min, int Max) Indices(double step, double margin, double length) =>
        ((int)Math.Ceiling(margin / step), (int)Math.Floor((length - margin) / step));

    private static RulingLines Ruled(Dictionary<string, string> c, Color color, double lineWidth, double width, double height)
    {
        double spacing = Number(c, "r1") ?? 24;
        var lines = new List<(Point, Point)>();
        var (min, max) = Indices(spacing, 0, height - HeaderSize - FooterSize);
        for (int i = Math.Max(0, min); i <= max; i++)
        {
            double y = HeaderSize + i * spacing;
            lines.Add((new Point(0, y), new Point(width, y)));
        }
        return new RulingLines(color, lineWidth, lines, []);
    }

    private static RulingLines Staves(Color color, double lineWidth, double width, double height)
    {
        const double Margin = 50, StavesSpacing = 40, LinesSpacing = 5;
        double staffHeight = lineWidth + 4 * LinesSpacing;
        double offset = StavesSpacing + staffHeight + 4 * lineWidth; // upstream keeps "+ 4 * lineWidth" for old files
        var lines = new List<(Point, Point)>();
        var (min, max) = Indices(offset, 0, height - HeaderSize - FooterSize);
        for (int i = Math.Max(0, min); i <= max; i++)
        {
            double top = HeaderSize + i * offset;
            for (int k = 0; k < 5; k++)
            {
                lines.Add((new Point(Margin, top + k * LinesSpacing), new Point(width - Margin, top + k * LinesSpacing)));
            }
            lines.Add((new Point(width - Margin, top + 4 * LinesSpacing), new Point(width - Margin, top)));
            lines.Add((new Point(Margin, top), new Point(Margin, top + 4 * LinesSpacing)));
        }
        return new RulingLines(color, lineWidth, lines, []);
    }

    private static IReadOnlyList<RulingLines> Graph(Dictionary<string, string> c, Color color, double lineWidth, double width, double height)
    {
        double square = Number(c, "r1") ?? 14.17;
        double margin = Number(c, "m1") ?? 0;
        int bold = (int)(Number(c, "bli") ?? 0);
        double boldWidth = Number(c, "blw") ?? 1.5;
        bool roundMargin = margin > 0 && (Number(c, "rm") ?? 0) != 0;

        double half = 0.5 * (bold > 0 ? boldWidth : lineWidth);
        double clearance = Math.Max(margin, half);
        var (minIx, maxIx) = Indices(square, clearance, width);
        var (minIy, maxIy) = Indices(square, clearance, height);
        double minX = margin > 0 ? margin : 0, maxX = margin > 0 ? width - margin : width;
        double minY = margin > 0 ? margin : 0, maxY = margin > 0 ? height - margin : height;
        if (roundMargin)
        {
            minX = Math.Max(minX, square * minIx);
            maxX = Math.Min(maxX, square * maxIx);
            minY = Math.Max(minY, square * minIy);
            maxY = Math.Min(maxY, square * maxIy);
        }
        var thin = new List<(Point, Point)>();
        var thick = new List<(Point, Point)>();
        if (minY < maxY)
        {
            for (int i = minIx; i <= maxIx; i++)
            {
                (bold > 0 && i % bold == 0 ? thick : thin).Add((new Point(i * square, minY), new Point(i * square, maxY)));
            }
        }
        if (minX < maxX)
        {
            for (int i = minIy; i <= maxIy; i++)
            {
                (bold > 0 && i % bold == 0 ? thick : thin).Add((new Point(minX, i * square), new Point(maxX, i * square)));
            }
        }
        return bold > 0
            ? [new RulingLines(color, lineWidth, thin, []), new RulingLines(color, boldWidth, thick, [])]
            : [new RulingLines(color, lineWidth, thin, [])];
    }

    private static RulingLines Dotted(Dictionary<string, string> c, Color color, double lineWidth, double width, double height)
    {
        double square = Number(c, "r1") ?? 14.17;
        var (minIx, maxIx) = Indices(square, 0.5 * lineWidth, width);
        var (minIy, maxIy) = Indices(square, 0.5 * lineWidth, height);
        var dots = new List<Point>();
        for (int i = minIx; i <= maxIx; i++)
        {
            for (int j = minIy; j <= maxIy; j++)
            {
                dots.Add(new Point(i * square, j * square));
            }
        }
        return new RulingLines(color, lineWidth, [], dots);
    }

    // Upstream BaseIsometricBackgroundView + IsoDotted/IsoGraph paintGrid, for the whole page.
    private static RulingLines Isometric(Dictionary<string, string> c, Color color, double lineWidth, double width, double height, bool dots)
    {
        double triangle = Number(c, "r1") ?? 14.17;
        double xstep = Math.Sqrt(3.0) / 2.0 * triangle, ystep = triangle / 2.0;
        int cols = (int)Math.Floor((width - 2 * triangle) / xstep);
        int rows = (int)Math.Floor((height - 2 * triangle) / ystep);
        double contentWidth = cols * xstep, contentHeight = rows * ystep;
        double x0 = (width - contentWidth) / 2, y0 = (height - contentHeight) / 2;
        Point P(double x, double y) => new(x0 + x, y0 + y);

        if (dots)
        {
            var points = new List<Point>();
            for (int col = 0; col <= cols; col++)
            {
                bool even = col % 2 == 0;
                double offset = even ? ystep : 0;
                int rowsInCol = even ? rows - 2 : rows;
                for (int row = 0; row <= rowsInCol; row += 2)
                {
                    points.Add(P(col * xstep, offset + row * ystep));
                }
            }
            return new RulingLines(color, lineWidth, [], points);
        }

        var lines = new List<(Point, Point)>
        {
            (P(0, 0), P(contentWidth, 0)),
            (P(0, contentHeight), P(contentWidth, contentHeight)),
        };
        for (int col = 0; col <= cols; col++)
        {
            lines.Add((P(col * xstep, 0), P(col * xstep, contentHeight)));
        }
        int hdiags = cols / 2, vdiags = rows / 2, diags = hdiags + vdiags;
        int hcorr = cols - 2 * hdiags, vcorr = rows - 2 * vdiags;
        for (int d = 0; d < diags + hcorr * vcorr; d++)
        {
            double x1 = contentWidth, y1 = 0;
            if (d < hdiags) { x1 = xstep + d * 2 * xstep; } else { y1 = ystep + (d - hdiags) * 2 * ystep - hcorr * ystep; }
            double x2 = 0, y2 = contentHeight;
            if (d < vdiags) { y2 = ystep + d * 2 * ystep; } else { x2 = xstep + (d - vdiags) * 2 * xstep - vcorr * xstep; }
            lines.Add((P(x1, y1), P(x2, y2)));
        }
        for (int d = 0; d < diags; d++)
        {
            double x1 = 0, y1 = 0;
            if (d < hdiags) { x1 = contentWidth - (xstep + d * 2 * xstep) - hcorr * xstep; } else { y1 = ystep + (d - hdiags) * 2 * ystep; }
            double x2 = contentWidth, y2 = contentHeight;
            if (d < vdiags) { y2 = ystep + d * 2 * ystep + hcorr * ystep; } else { x2 = contentWidth - (xstep + (d - vdiags) * 2 * xstep) + (vcorr - hcorr) * xstep; }
            lines.Add((P(x1, y1), P(x2, y2)));
        }
        return new RulingLines(color, lineWidth, lines, []);
    }

    // "key=value,key=value" (upstream BackgroundConfig).
    private static Dictionary<string, string> Parse(string? config)
    {
        var values = new Dictionary<string, string>();
        foreach (var part in (config ?? "").Split(','))
        {
            int eq = part.LastIndexOf('=');
            if (eq > 0)
            {
                values[part[..eq].Trim()] = part[(eq + 1)..].Trim();
            }
        }
        return values;
    }

    private static double? Number(Dictionary<string, string> c, string key) =>
        c.TryGetValue(key, out var s) && double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : null;

    // A hex color: AARRGGBB as upstream's Color(uint32); six digits are taken as opaque RRGGBB.
    private static Color? HexColor(Dictionary<string, string> c, string key)
    {
        if (!c.TryGetValue(key, out var raw))
        {
            return null;
        }
        string s = raw.TrimStart('#');
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            s = s[2..]; // std::stoul(..., 16) as upstream accepts the prefix
        }
        if (!uint.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v))
        {
            return null;
        }
        if (s.Length <= 6)
        {
            v |= 0xff000000;
        }
        return Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    }

    private static Color Rgb(uint v) => Color.FromArgb(255, (byte)(v >> 16), (byte)(v >> 8), (byte)v);
}
