using System.Globalization;
using System.Xml.Linq;
using Windows.Foundation;

namespace XournalStudio.Model;

/// <summary>
/// Xournal's picture elements: &lt;image&gt; (PNG/JPEG) and &lt;teximage&gt; (LaTeX or copied PDF areas,
/// a one-page PDF or PNG), both with left/top/right/bottom and the data as base64 content.
/// New pictures are always written as plain &lt;image&gt; with a PNG, which every Xournal++ reads.
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

    public static Rect GetBounds(XElement e)
    {
        double left = Number(e, "left"), top = Number(e, "top");
        double right = Number(e, "right"), bottom = Number(e, "bottom");
        return new Rect(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top));
    }

    /// <summary>Sets the position of the element itself (all other attributes kept).</summary>
    public static void SetBounds(XElement e, Rect bounds)
    {
        e.SetAttributeValue("left", Format(bounds.Left));
        e.SetAttributeValue("top", Format(bounds.Top));
        e.SetAttributeValue("right", Format(bounds.Right));
        e.SetAttributeValue("bottom", Format(bounds.Bottom));
    }

    /// <summary>The bounds after scaling/moving by <paramref name="m"/> (no rotation: pictures stay upright).</summary>
    public static Rect Transform(Rect bounds, System.Numerics.Matrix3x2 m)
    {
        var a = System.Numerics.Vector2.Transform(new((float)bounds.Left, (float)bounds.Top), m);
        var b = System.Numerics.Vector2.Transform(new((float)bounds.Right, (float)bounds.Bottom), m);
        return new Rect(new Point(a.X, a.Y), new Point(b.X, b.Y));
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
        double.TryParse((string?)e.Attribute(name), NumberStyles.Float, Inv, out double v) ? v : 0;

    private static string Format(double v) => v.ToString("0.####", Inv);
}
