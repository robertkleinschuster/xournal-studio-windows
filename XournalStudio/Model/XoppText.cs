using System.Globalization;
using System.Numerics;
using System.Xml.Linq;
using Windows.UI;

namespace XournalStudio.Model;

public enum XoppTextAlignment { Left, Center, Right }

/// <summary>
/// A text of a .xopp page as upstream Xournal++ reads it (XmlParser::parseTextTag): &lt;text font="Sans" size="12"
/// color="#…" x y | matrix [wrap] [align] [justify]&gt;content&lt;/text&gt;; &lt;link&gt; (a text with a url) is shown the
/// same way. The font is a Pango description ("Sans Bold Italic"); the box's top left is at x,y, or the
/// <see cref="Transform"/> places it (file version 5). Read only: the app shows texts, it does not edit them.
/// </summary>
public sealed record XoppText(
    string Content, string Family, bool Bold, bool Italic, double Size, Color Color, Matrix3x2 Transform,
    double? Wrap, XoppTextAlignment Alignment, bool Justify)
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    // Pango style words that may end a font description.
    private static readonly HashSet<string> StyleWords = new(StringComparer.OrdinalIgnoreCase)
    {
        "Bold", "Italic", "Oblique", "Normal", "Regular", "Light", "Medium", "Semi-Bold", "SemiBold", "Ultra-Bold",
        "Heavy", "Thin", "Book", "Condensed", "Semi-Condensed", "Expanded", "Small-Caps",
    };

    public static bool IsText(XElement e) => e.Name == "text" || e.Name == "link";

    public static XoppText Read(XElement e)
    {
        var words = ((string?)e.Attribute("font") ?? "Sans").Split(' ', StringSplitOptions.RemoveEmptyEntries).ToList();
        bool bold = false, italic = false;
        while (words.Count > 1 && StyleWords.Contains(words[^1]))
        {
            bold |= words[^1].Contains("Bold", StringComparison.OrdinalIgnoreCase) || words[^1].Equals("Heavy", StringComparison.OrdinalIgnoreCase);
            italic |= words[^1].Equals("Italic", StringComparison.OrdinalIgnoreCase) ||
                words[^1].Equals("Oblique", StringComparison.OrdinalIgnoreCase);
            words.RemoveAt(words.Count - 1);
        }
        string family = words.Count > 0 ? string.Join(' ', words).TrimEnd(',') : "Sans";

        var transform = XoppFormat.ReadMatrix(e) ?? Matrix3x2.CreateTranslation((float)Number(e, "x", 0), (float)Number(e, "y", 0));
        double? wrap = double.TryParse((string?)e.Attribute("wrap"), NumberStyles.Float, Inv, out double w) && w > 0 ? w : null;
        var alignment = (string?)e.Attribute("align") switch
        {
            "center" => XoppTextAlignment.Center,
            "right" => XoppTextAlignment.Right,
            _ => XoppTextAlignment.Left,
        };
        bool justify = (string?)e.Attribute("justify") is "true" or "1";
        return new XoppText(e.Value, family, bold, italic, Number(e, "size", 12),
            XoppColor.Parse((string?)e.Attribute("color"), Microsoft.UI.Colors.Black), transform, wrap, alignment, justify);
    }

    /// <summary>The text's paragraphs (line breaks "\r\n", "\n" or "\r", as the screen's TextBlock breaks them).</summary>
    public string[] Lines => Content.Replace("\r\n", "\n").Split('\n', '\r');

    /// <summary>The font family Windows has for a Pango family name (Pango's aliases on Windows).</summary>
    public string WindowsFamily => Family.ToLowerInvariant() switch
    {
        "sans" or "sans-serif" => "Arial",
        "serif" => "Times New Roman",
        "monospace" or "mono" => "Courier New",
        _ => Family,
    };

    private static double Number(XElement e, string name, double fallback) =>
        double.TryParse((string?)e.Attribute(name), NumberStyles.Float, Inv, out double v) ? v : fallback;
}
