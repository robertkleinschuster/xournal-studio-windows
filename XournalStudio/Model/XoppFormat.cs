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
}
