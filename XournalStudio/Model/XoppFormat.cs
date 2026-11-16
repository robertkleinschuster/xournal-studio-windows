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
            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out v[i]) || !float.IsFinite(v[i]))
            {
                return null;
            }
        }
        return new Matrix3x2(v[0], v[1], v[2], v[3], v[4], v[5]);
    }

    public static void WriteMatrix(XElement e, Matrix3x2 m) => e.SetAttributeValue("matrix", string.Join(' ',
        new[] { m.M11, m.M12, m.M21, m.M22, m.M31, m.M32 }.Select(v => v.ToString("0.######", CultureInfo.InvariantCulture))));
}
