using System.Globalization;
using System.Numerics;
using System.Xml.Linq;

namespace XournalStudio.Model;

/// <summary>
/// The .xopp format of upstream Xournal++ (src/core/control/xojfile: XmlTags.h, XmlAttrs.h, SaveHandler.cpp).
/// Every feature is expressed with upstream elements and attributes; this app invents none of its own and
/// keeps everything it reads (also attributes of other programs) when saving.
/// </summary>
public static class XoppFormat
{
    /// <summary>File format version written for new documents (images with left/top/right/bottom need 4).</summary>
    public const string FileVersion = "4";

    /// <summary>The creator attribute; upstream only stores it.</summary>
    public const string Creator = "Xournal Studio for Windows";

    /// <summary>
    /// Largest magnitude a number of a file may have (positions, sizes, matrices of texts and pictures; stroke
    /// coordinates, widths and dashes): pages are a few hundred points, so a value beyond a million cannot be meant
    /// (damaged file). Keeps every computation in float range. Values beyond it are not shown, but kept in the file.
    /// </summary>
    public const double NumberLimit = 1_000_000;

    /// <summary>
    /// A number of the file (invariant culture), or null when it is missing, not a number, or beyond
    /// <see cref="NumberLimit"/> – the one rule for the numbers of texts, pictures and strokes.
    /// </summary>
    public static double? ReadNumber(string? text) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) && Math.Abs(v) <= NumberLimit
            ? v : null;

    /// <summary>An attribute's number (see <see cref="ReadNumber(string?)"/>) or <paramref name="fallback"/>.</summary>
    public static double ReadNumber(XElement e, string name, double fallback) =>
        ReadNumber((string?)e.Attribute(name)) ?? fallback;

    /// <summary>The "matrix" attribute of file version 5 (texts, pictures): xx yx xy yy x0 y0, cairo's order.</summary>
    public static Matrix3x2? ReadMatrix(XElement e)
    {
        var parts = ((string?)e.Attribute("matrix") ?? "").Split(' ', StringSplitOptions.RemoveEmptyEntries);
        var v = new float[6];
        if (parts.Length != 6)
        {
            return null;
        }
        for (int i = 0; i < 6; i++)
        {
            if (ReadNumber(parts[i]) is not { } number)
            {
                return null;
            }
            v[i] = (float)number;
        }
        return new Matrix3x2(v[0], v[1], v[2], v[3], v[4], v[5]);
    }

    public static void WriteMatrix(XElement e, Matrix3x2 m) => e.SetAttributeValue("matrix", string.Join(' ',
        new[] { m.M11, m.M12, m.M21, m.M22, m.M31, m.M32 }.Select(v => v.ToString("0.######", CultureInfo.InvariantCulture))));
}
