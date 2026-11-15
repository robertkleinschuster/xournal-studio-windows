using Windows.Foundation;
using Windows.UI;

namespace XournalStudio.Model;

/// <summary>The lines or dots of a ruled page background, in page points.</summary>
public sealed record Ruling(Color Color, IReadOnlyList<(Point From, Point To)> Lines, IReadOnlyList<Point> Dots)
{
    public const double LineWidth = 0.5;
    public const double DotRadius = 0.4;
}

/// <summary>Xournal's ruling patterns (sizes from Xournal++'s background views), shown on screen and exported.</summary>
public static class BackgroundRuling
{
    private const double GraphSpacing = 14.17; // 5 mm
    private static readonly Color Silver = Color.FromArgb(255, 0xc0, 0xc0, 0xc0);
    private static readonly Color Blue = Color.FromArgb(255, 0x40, 0xa0, 0xff);

    /// <summary>The ruling of a page; null for a plain one.</summary>
    public static Ruling? For(BackgroundStyle style, double width, double height)
    {
        var lines = new List<(Point, Point)>();
        var dots = new List<Point>();
        switch (style)
        {
            case BackgroundStyle.Graph:
                for (double x = GraphSpacing; x < width; x += GraphSpacing)
                {
                    lines.Add((new Point(x, 0), new Point(x, height)));
                }
                for (double y = GraphSpacing; y < height; y += GraphSpacing)
                {
                    lines.Add((new Point(0, y), new Point(width, y)));
                }
                return new Ruling(Silver, lines, dots);
            case BackgroundStyle.Lined:
            case BackgroundStyle.Ruled:
                for (double y = 80; y < height - 20; y += 24)
                {
                    lines.Add((new Point(0, y), new Point(width, y)));
                }
                return new Ruling(Blue, lines, dots);
            case BackgroundStyle.Dotted:
                for (double x = GraphSpacing; x < width; x += GraphSpacing)
                {
                    for (double y = GraphSpacing; y < height; y += GraphSpacing)
                    {
                        dots.Add(new Point(x, y));
                    }
                }
                return new Ruling(Silver, lines, dots);
            default:
                return null;
        }
    }
}
