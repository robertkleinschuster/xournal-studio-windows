using System.Globalization;
using System.Numerics;
using System.Xml.Linq;
using Windows.Foundation;

namespace XournalStudio.Model;

/// <summary>
/// A group of .xopp elements (&lt;stroke&gt;, &lt;image&gt;, &lt;teximage&gt;) relative to the group's top-left corner:
/// what is copied to the clipboard and pasted or duplicated. Only lives on the clipboard, never in a file; the
/// elements in it are upstream's.
/// </summary>
public static class XoppClip
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    /// <summary>The elements (copies) moved so that <paramref name="bounds"/>' top-left corner is at 0,0.</summary>
    public static XElement Create(Rect bounds, IEnumerable<XElement> elements)
    {
        var clip = new XElement("clip",
            new XAttribute("width", bounds.Width.ToString("0.####", Inv)),
            new XAttribute("height", bounds.Height.ToString("0.####", Inv)));
        var toOrigin = Matrix3x2.CreateTranslation(-(float)bounds.X, -(float)bounds.Y);
        foreach (var e in elements)
        {
            clip.Add(Transformed(e, toOrigin));
        }
        return clip;
    }

    public static Size GetSize(XElement clip) => new(
        double.Parse((string?)clip.Attribute("width") ?? "0", Inv),
        double.Parse((string?)clip.Attribute("height") ?? "0", Inv));

    /// <summary>Copies of the elements, transformed (placed and scaled) by <paramref name="m"/>.</summary>
    public static List<XElement> Place(XElement clip, Matrix3x2 m) =>
        clip.Elements().Select(e => Transformed(e, m)).ToList();

    /// <summary>A copy of a stroke or picture element moved/scaled by <paramref name="m"/>.</summary>
    public static XElement Transformed(XElement e, Matrix3x2 m)
    {
        var copy = new XElement(e);
        if (copy.Name == "stroke")
        {
            StrokeConverter.Transform(copy, m);
        }
        else if (XoppImage.IsImage(copy))
        {
            XoppImage.SetBounds(copy, XoppImage.Transform(XoppImage.GetBounds(copy), m));
        }
        return copy;
    }
}
