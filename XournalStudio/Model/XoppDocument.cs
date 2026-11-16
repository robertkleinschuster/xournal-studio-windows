using System.IO.Compression;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace XournalStudio.Model;

/// <summary>
/// A Xournal++ document (.xopp: gzip-compressed XML). The XML tree is kept as loaded so that
/// everything this app does not understand (text, images, layers, attributes) is written back
/// unchanged; only strokes are taken over by the ink model of each page.
/// </summary>
public sealed class XoppDocument
{
    public const double A4Width = 595.27559;
    public const double A4Height = 841.88976;

    private readonly XDocument xml;

    public string? FilePath { get; set; }

    /// <summary>Resolved path of the PDF used as page background, if any.</summary>
    public string? PdfPath { get; private set; }

    public List<XoppPage> Pages { get; } = new();

    /// <summary>When the document was created in this app (for the default file name); null for loaded files.</summary>
    public DateTime? CreatedAt { get; set; }

    // The PDF reference as found in the file (may be relative); written back unchanged while it still fits.
    private (string? Domain, string? Filename) loadedPdfReference;

    private XElement Root => xml.Root!;

    private XoppDocument(XDocument xml)
    {
        this.xml = xml;
    }

    public static XoppDocument CreateNew()
    {
        var doc = new XoppDocument(NewXml()) { CreatedAt = DateTime.Now };
        doc.Pages.Add(XoppPage.CreateGraph(A4Width, A4Height));
        return doc;
    }

    /// <summary>A document with one page per PDF page, the PDF as background.</summary>
    public static XoppDocument FromPdf(string pdfPath, IReadOnlyList<(double Width, double Height)> pageSizes)
    {
        var doc = new XoppDocument(NewXml()) { PdfPath = pdfPath, CreatedAt = DateTime.Now };
        for (int i = 0; i < pageSizes.Count; i++)
        {
            doc.Pages.Add(XoppPage.CreatePdf(pageSizes[i].Width, pageSizes[i].Height, i));
        }
        return doc;
    }

    /// <summary>
    /// The background PDF was renamed or moved: the document refers to it at <paramref name="newPath"/> from now on
    /// (written on the next save, as the old reference no longer names it).
    /// </summary>
    public void MovePdf(string newPath) => PdfPath = newPath;

    /// <summary>
    /// The name the first save suggests: the background PDF's name, otherwise upstream Xournal++'s default
    /// "%F-Notiz-%H-%M" (German translation of "%F-Note-%H-%M") from the time the document was created.
    /// </summary>
    public string SuggestedName => NameFor(FilePath, PdfPath, CreatedAt ?? DateTime.Now);

    /// <summary>The document's name without extension: its file's, else its PDF's, else Xournal's date-time name.</summary>
    public static string NameFor(string? filePath, string? pdfPath, DateTime created) =>
        filePath != null ? Path.GetFileNameWithoutExtension(filePath)
        : pdfPath != null ? Path.GetFileNameWithoutExtension(pdfPath)
        : DefaultName(created);

    /// <summary>
    /// Where PDF references of a copy are made relative to: the document's file, or for a document never saved a file
    /// next to its PDF (any place does when it has none).
    /// </summary>
    public string ReferenceBase => FilePath ?? Path.Combine(
        Path.GetDirectoryName(PdfPath) ?? Path.GetTempPath(), SuggestedName + ".xopp");

    public static string DefaultName(DateTime created) => created.ToString("yyyy-MM-dd'-Notiz-'HH-mm", System.Globalization.CultureInfo.InvariantCulture);

    public static XoppDocument Load(string path) => Load(path, path);

    /// <summary>
    /// Loads a copy written by <see cref="Snapshot"/> (crash recovery): PDF references are resolved as from
    /// <paramref name="referenceBase"/>, and the document's file is <paramref name="documentPath"/> (null: never saved).
    /// </summary>
    public static XoppDocument LoadCopy(string path, string? documentPath, string referenceBase)
    {
        var doc = Load(path, referenceBase);
        doc.FilePath = documentPath;
        return doc;
    }

    private static XoppDocument Load(string path, string referenceBase)
    {
        XDocument xml;
        using (var file = File.OpenRead(path))
        {
            Stream input = file;
            int b1 = file.ReadByte(), b2 = file.ReadByte();
            file.Position = 0;
            if (b1 == 0x1f && b2 == 0x8b)
            {
                input = new GZipStream(file, CompressionMode.Decompress);
            }
            xml = XDocument.Load(input, LoadOptions.PreserveWhitespace);
        }

        if (xml.Root?.Name.LocalName != "xournal")
        {
            throw new InvalidDataException("Not a Xournal++ document.");
        }

        var doc = new XoppDocument(xml) { FilePath = path };
        foreach (var pageElement in xml.Root.Elements("page").ToList())
        {
            pageElement.Remove();
            doc.Pages.Add(XoppPage.FromXml(pageElement));
        }

        var firstPdf = doc.Pages.Select(p => p.Background).FirstOrDefault(b => (string?)b.Attribute("type") == "pdf");
        if (firstPdf != null)
        {
            doc.loadedPdfReference = ((string?)firstPdf.Attribute("domain"), (string?)firstPdf.Attribute("filename"));
            doc.PdfPath = ResolvePdfPath(referenceBase, doc.loadedPdfReference.Domain, doc.loadedPdfReference.Filename);
        }
        ResolvePictureBackgrounds(doc.Pages, referenceBase);
        return doc;
    }

    /// <summary>Writes "&lt;path&gt;~" first and replaces the file only once it is complete.</summary>
    public void Save(string path)
    {
        Write(Snapshot(path), path);
        FilePath = path;
    }

    /// <summary>
    /// The complete document as XML, independent of the live pages (so it can be written on another thread). PDF
    /// references are written as from <paramref name="referenceBase"/>, the path the file is (or would be) saved at.
    /// </summary>
    public XDocument Snapshot(string referenceBase)
    {
        Root.SetAttributeValue("creator", XoppFormat.Creator);
        if (Root.Attribute("fileversion") == null)
        {
            Root.SetAttributeValue("fileversion", XoppFormat.FileVersion);
        }

        var output = new XDocument(new XDeclaration("1.0", null, "no"), new XElement(Root));
        bool firstPdf = true;
        foreach (var page in Pages)
        {
            var element = page.Snapshot();
            var background = element.Element("background");
            if (background != null && (string?)background.Attribute("type") == "pdf")
            {
                // Only the first PDF background names the file; Xournal reads the attributes in this order.
                var pageNo = (string?)background.Attribute("pageno");
                background.RemoveAttributes();
                background.SetAttributeValue("type", "pdf");
                if (firstPdf && KeepsLoadedPdfReference(referenceBase))
                {
                    background.SetAttributeValue("domain", loadedPdfReference.Domain ?? "absolute");
                    background.SetAttributeValue("filename", loadedPdfReference.Filename);
                }
                else if (firstPdf && PdfPath != null)
                {
                    background.SetAttributeValue("domain", "absolute");
                    background.SetAttributeValue("filename", PdfReference(PdfPath, referenceBase));
                }
                background.SetAttributeValue("pageno", pageNo);
                firstPdf = false;
            }
            output.Root!.Add(element);
        }
        return output;
    }

    /// <summary>Writes a <see cref="Snapshot"/> gzip-compressed to "&lt;path&gt;~", then replaces the file.</summary>
    public static void Write(XDocument snapshot, string path)
    {
        string temp = path + "~";
        using (var file = File.Create(temp))
        using (var gzip = new GZipStream(file, CompressionLevel.Fastest))
        using (var writer = XmlWriter.Create(gzip, new XmlWriterSettings
        {
            Encoding = new UTF8Encoding(false),
            Indent = true,
            IndentChars = "",
        }))
        {
            snapshot.Save(writer);
        }
        File.Move(temp, path, overwrite: true);
    }

    // The reference read from the file is written back as it was while it still names the PDF from where the
    // document is saved (an attached PDF always: it travels with the document's name).
    private bool KeepsLoadedPdfReference(string target)
    {
        var (domain, filename) = loadedPdfReference;
        if (filename == null)
        {
            return false;
        }
        if (PdfPath == null)
        {
            return true;
        }
        if (domain == "attach")
        {
            // "<name>.xopp.bg.pdf" belongs to a file's name: kept only where it names the PDF (not for Save As or a copy).
            return string.Equals(Path.GetFullPath(target + "." + filename), Path.GetFullPath(PdfPath),
                StringComparison.OrdinalIgnoreCase);
        }
        string dir = Path.GetDirectoryName(Path.GetFullPath(target))!;
        string named = Path.GetFullPath(Path.Combine(dir, filename.Replace('/', Path.DirectorySeparatorChar)));
        return string.Equals(named, Path.GetFullPath(PdfPath), StringComparison.OrdinalIgnoreCase);
    }

    // As upstream SaveHandler + Util::normalizeAssetPath: domain "absolute" just means "a path"; it is relative to the
    // .xopp's folder (upstream's default storage mode) unless the loaded file used an absolute one, with '/'
    // separators. Relative keeps git-synced folders portable between machines.
    private string PdfReference(string pdf, string target)
    {
        string full = Path.GetFullPath(pdf);
        bool absolute = loadedPdfReference.Filename != null && Path.IsPathRooted(loadedPdfReference.Filename);
        string reference = absolute
            ? full
            : Path.GetRelativePath(Path.GetDirectoryName(Path.GetFullPath(target))!, full);
        return reference.Replace(Path.DirectorySeparatorChar, '/');
    }

    private static XDocument NewXml() =>
        new(new XElement("xournal",
            new XAttribute("creator", XoppFormat.Creator),
            new XAttribute("fileversion", XoppFormat.FileVersion),
            new XElement("title", "Xournal++ document - see https://xournalpp.github.io/")));

    // Picture backgrounds as upstream's LoadHandler::setBgPixmap: "attach" = "<document>.<filename>", "absolute" = the
    // file (relative to the document's folder if not rooted), "clone" = the background of page number <filename>.
    private static void ResolvePictureBackgrounds(List<XoppPage> pages, string referenceBase)
    {
        string dir = Path.GetDirectoryName(Path.GetFullPath(referenceBase))!;
        foreach (var page in pages.Where(p => p.IsImageBackground))
        {
            var bg = page.Background;
            string? domain = (string?)bg.Attribute("domain");
            string? filename = (string?)bg.Attribute("filename");
            if (string.IsNullOrEmpty(filename))
            {
                continue;
            }
            if (domain == "clone")
            {
                if (int.TryParse(filename, out int index) && index >= 0 && index < pages.Count)
                {
                    page.BackgroundImagePath = pages[index].BackgroundImagePath;
                }
                continue;
            }
            string path = domain == "attach"
                ? referenceBase + "." + filename
                : Path.GetFullPath(Path.Combine(dir, filename));
            page.BackgroundImagePath = File.Exists(path) ? path : null; // not found: white, as upstream logs an error
        }
    }

    private static string? ResolvePdfPath(string xoppPath, string? domain, string? filename)
    {
        if (string.IsNullOrEmpty(filename))
        {
            return null;
        }

        string dir = Path.GetDirectoryName(Path.GetFullPath(xoppPath))!;
        var candidates = new List<string>();
        if (domain == "attach")
        {
            candidates.Add(Path.ChangeExtension(xoppPath, null) + ".xopp." + filename);
            candidates.Add(xoppPath + "." + filename);
        }
        else
        {
            // Relative to the .xopp's folder, '/' separated ("../VO/x.pdf").
            candidates.Add(Path.GetFullPath(Path.Combine(dir, filename)));
            // Moved documents: Xournal also looks for the PDF next to the .xopp.
            candidates.Add(Path.Combine(dir, Path.GetFileName(filename.Replace('/', '\\'))));
        }
        return candidates.FirstOrDefault(File.Exists) ?? candidates[0];
    }
}
