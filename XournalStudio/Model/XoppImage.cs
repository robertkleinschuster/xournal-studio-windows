using System.Globalization;
using System.Xml.Linq;
using Windows.Foundation;

namespace XournalStudio.Model;

/// <summary>
/// Xournal's picture elements: &lt;image&gt; (PNG/JPEG) and &lt;teximage&gt; (LaTeX or copied PDF areas,
/// a one-page PDF or PNG), with the data as base64 content, placed by left/top/right/bottom (file version 4) or by a
/// "matrix" from the picture's natural size (pixels) to the page (version 5, upstream's master; then natural_size is
/// written too). New pictures are always written as plain &lt;image&gt; with left/top/right/bottom, which every
/// Xournal++ reads; a picture placed by a matrix keeps its matrix when moved.
/// </summary>
public static class XoppImage
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    public static bool IsImage(XElement e) => e.Name == "image" || e.Name == "teximage";

    public static XElement Create(Rect bounds, byte[] png, int pixelWidth, int pixelHeight) =>
        new("image",
            new XAttribute("left", Format(bounds.Left)),
            new XAttribute("top", Format(bounds.Top)),
            new XAttribute("right", Format(bounds.Right)),
            new XAttribute("bottom", Format(bounds.Bottom)),
            new XAttribute("natural_size", $"{pixelWidth} {pixelHeight}"),
            Convert.ToBase64String(png));

    /// <summary>
    /// How the picture is drawn: its data stretched over <paramref name="Size"/>, then placed by <paramref name="Transform"/>
    /// (version 4: the bounds' size, moved to their corner; version 5: natural size and matrix).
    /// </summary>
    public readonly record struct Placement(Size Size, System.Numerics.Matrix3x2 Transform);

    public static Placement GetPlacement(XElement e)
    {
        if (XoppFormat.ReadMatrix(e) is { } m && NaturalSize(e) is { } natural)
        {
            return new Placement(natural, m);
        }
        var b = LegacyBounds(e);
        return new Placement(new Size(b.Width, b.Height), System.Numerics.Matrix3x2.CreateTranslation((float)b.X, (float)b.Y));
    }

    /// <summary>The area the picture covers on the page (axis-aligned, also for a turned matrix).</summary>
    public static Rect GetBounds(XElement e)
    {
        if (XoppFormat.ReadMatrix(e) is { } m && NaturalSize(e) is { } natural)
        {
            var corners = new[] { new System.Numerics.Vector2(0, 0), new((float)natural.Width, 0),
                new((float)natural.Width, (float)natural.Height), new(0, (float)natural.Height) }
                .Select(c => System.Numerics.Vector2.Transform(c, m)).ToList();
            return new Rect(new Point(corners.Min(c => c.X), corners.Min(c => c.Y)), new Point(corners.Max(c => c.X), corners.Max(c => c.Y)));
        }
        return LegacyBounds(e);
    }

    private static Size? NaturalSize(XElement e)
    {
        var parts = ((string?)e.Attribute("natural_size") ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        return parts.Length == 2 && double.TryParse(parts[0], NumberStyles.Float, Inv, out double w) &&
            double.TryParse(parts[1], NumberStyles.Float, Inv, out double h) && w > 0 && h > 0 && double.IsFinite(w) &&
            double.IsFinite(h) ? new Size(w, h) : null;
    }

    private static Rect LegacyBounds(XElement e)
    {
        double left = Number(e, "left"), top = Number(e, "top");
        double right = Number(e, "right"), bottom = Number(e, "bottom");
        return new Rect(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top));
    }

    /// <summary>
    /// Moves/scales the element to cover <paramref name="bounds"/> (all other attributes kept). A picture placed by a
    /// matrix keeps it: the mapping from its old bounds to the new ones is added to it.
    /// </summary>
    public static void SetBounds(XElement e, Rect bounds)
    {
        if (XoppFormat.ReadMatrix(e) is { } m && NaturalSize(e) != null)
        {
            var old = GetBounds(e);
            if (old.Width > 0 && old.Height > 0)
            {
                var map = System.Numerics.Matrix3x2.CreateTranslation(-(float)old.X, -(float)old.Y) *
                    System.Numerics.Matrix3x2.CreateScale((float)(bounds.Width / old.Width), (float)(bounds.Height / old.Height)) *
                    System.Numerics.Matrix3x2.CreateTranslation((float)bounds.X, (float)bounds.Y);
                XoppFormat.WriteMatrix(e, m * map);
                return;
            }
        }
        e.SetAttributeValue("left", Format(bounds.Left));
        e.SetAttributeValue("top", Format(bounds.Top));
        e.SetAttributeValue("right", Format(bounds.Right));
        e.SetAttributeValue("bottom", Format(bounds.Bottom));
    }

    /// <summary>The bounds after scaling/moving by <paramref name="m"/> (no rotation: pictures stay upright).</summary>
    public static Rect Transform(Rect bounds, System.Numerics.Matrix3x2 m)
    {
        // In double, as stroke coordinates (Vector2 would round the file's numbers to float).
        Point Map(double x, double y) => new(x * m.M11 + y * m.M21 + m.M31, x * m.M12 + y * m.M22 + m.M32);
        return new Rect(Map(bounds.Left, bounds.Top), Map(bounds.Right, bounds.Bottom));
    }

    public static byte[] GetData(XElement e)
    {
        try
        {
            return Convert.FromBase64String(e.Value.Trim());
        }
        catch (FormatException)
        {
            return [];
        }
    }

    public static bool IsPdf(byte[] data) => data.Length > 4 && data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F';

    private static double Number(XElement e, string name) =>
        double.TryParse((string?)e.Attribute(name), NumberStyles.Float, Inv, out double v) && double.IsFinite(v) ? v : 0;

    private static string Format(double v) => v.ToString("0.####", Inv);
}
