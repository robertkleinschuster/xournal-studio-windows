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

    /// <summary>Writes "&lt;path&gt;~" first and replaces the file only once it is complete (Ctrl+S: nothing else).</summary>
    public void Save(string path)
    {
        Write(Snapshot(path), path);
        FilePath = path;
    }

    /// <summary>
    /// Save As (upstream's way): the attachments go along (<see cref="CopyAttachments"/>), the file keeps "attach", and
    /// the document refers to its own attachments from then on. If writing fails, the copies just made are removed.
    /// </summary>
    public void SaveAs(string path)
    {
        var copies = WriteWithAttachments(path);
        FilePath = path;
        foreach (var (from, to) in copies)
        {
            if (PdfPath != null && SamePath(PdfPath, from))
            {
                PdfPath = to;
            }
            foreach (var page in Pages.Where(p => p.BackgroundImagePath != null && SamePath(p.BackgroundImagePath, from)))
            {
                page.BackgroundImagePath = to;
            }
        }
    }

    /// <summary>
    /// A copy of the document at <paramref name="path"/> with its own attachments (the document itself stays where it is).
    /// If writing fails, the copies just made are removed.
    /// </summary>
    public void SaveCopy(string path) => WriteWithAttachments(path);

    private Dictionary<string, string> WriteWithAttachments(string path)
    {
        var copies = CopyAttachments(path);
        try
        {
            Write(Snapshot(path, copies), path);
        }
        catch
        {
            foreach (var copy in copies.Values)
            {
                File.Delete(copy); // only the files made just now for this write
            }
            throw;
        }
        return copies;
    }

    /// <summary>
    /// Copies the attached PDF and picture backgrounds (domain "attach": "&lt;document&gt;.&lt;filename&gt;") next to
    /// <paramref name="target"/> as "&lt;target&gt;.&lt;filename&gt;", as upstream does when it saves under another name.
    /// Never over an existing file (then the reference is written as a path). Returns source → copy (full paths).
    /// </summary>
    public Dictionary<string, string> CopyAttachments(string target)
    {
        var copies = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        void Copy(string? source, string filename)
        {
            if (source == null || !File.Exists(source))
            {
                return; // missing: the reference is written as a path instead (Snapshot)
            }
            string from = Path.GetFullPath(source), to = Path.GetFullPath(target + "." + filename);
            if (SamePath(from, to) || copies.ContainsKey(from) || File.Exists(to))
            {
                return; // already there, already copied, or another file of that name: never overwritten
            }
            File.Copy(from, to);
            copies[from] = to;
        }
        if (loadedPdfReference is ("attach", { } pdfName))
        {
            Copy(PdfPath, pdfName);
        }
        foreach (var page in Pages.Where(p => p.IsImageBackground && p.BackgroundCloneSource == null))
        {
            if ((string?)page.Background.Attribute("domain") == "attach" && (string?)page.Background.Attribute("filename") is { } name)
            {
                Copy(page.BackgroundImagePath, name);
            }
        }
        return copies;
    }

    private static bool SamePath(string a, string b) =>
        string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);

    /// <summary>
    /// The complete document as XML, independent of the live pages (so it can be written on another thread). PDF
    /// references are written as from <paramref name="referenceBase"/>, the path the file is (or would be) saved at;
    /// <paramref name="attachmentCopies"/>: attachments copied there for this write (source → copy, see CopyAttachments).
    /// </summary>
    public XDocument Snapshot(string referenceBase, IReadOnlyDictionary<string, string>? attachmentCopies = null)
    {
        var copies = attachmentCopies ?? new Dictionary<string, string>();
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
                if (firstPdf && KeepsLoadedPdfReference(referenceBase, copies))
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
            else if (background != null && page.IsImageBackground)
            {
                WritePictureReference(page, background, referenceBase, output.Root!.Elements("page").Count(), copies);
            }
            output.Root!.Add(element);
        }
        return output;
    }

    // An "attach" reference is kept where it names exactly the document's own attachment, or the copy made of it for
    // this write – never a file that merely has the name.
    private static bool IsOwnAttachment(string attachedFile, string? source, IReadOnlyDictionary<string, string> copies) =>
        source != null && (SamePath(attachedFile, source) ||
            (copies.TryGetValue(Path.GetFullPath(source), out var copy) && SamePath(attachedFile, copy)));

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
    private bool KeepsLoadedPdfReference(string target, IReadOnlyDictionary<string, string> copies)
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
            // "<name>.xopp.bg.pdf" belongs to a file's name: kept where it names the document's PDF or its copy made for
            // this write (CopyAttachments); otherwise the PDF is named by its path.
            return IsOwnAttachment(target + "." + filename, PdfPath, copies);
        }
        string dir = Path.GetDirectoryName(Path.GetFullPath(target))!;
        string named = Path.GetFullPath(Path.Combine(dir, filename.Replace('/', Path.DirectorySeparatorChar)));
        return string.Equals(named, Path.GetFullPath(PdfPath), StringComparison.OrdinalIgnoreCase);
    }

    // A picture background's reference is written back as it was while it still names its file from where the document
    // is saved (a clone: while its page is still an earlier page; "attach": while "<target>.<filename>" is there – the
    // picture, or the copy made of it for this write, see CopyAttachments). Otherwise it names the file as a path.
    private void WritePictureReference(XoppPage page, XElement background, string target, int pageIndex,
        IReadOnlyDictionary<string, string> copies)
    {
        string? domain = (string?)background.Attribute("domain");
        string? filename = (string?)background.Attribute("filename");
        string? path = page.BackgroundImagePath;
        if (path == null)
        {
            return; // names nothing this app can resolve: kept as it was
        }
        if (page.BackgroundCloneSource is { } source)
        {
            int index = Pages.IndexOf(source);
            if (index >= 0 && index < pageIndex)
            {
                background.SetAttributeValue("filename", index.ToString(System.Globalization.CultureInfo.InvariantCulture));
                return;
            }
            // its page was removed or is no longer before it: the clone names the picture itself
        }
        else if (filename != null && (domain == "attach"
            ? IsOwnAttachment(PictureFile(target, domain, filename), path, copies)
            : string.Equals(PictureFile(target, domain, filename), Path.GetFullPath(path), StringComparison.OrdinalIgnoreCase)))
        {
            return;
        }
        background.SetAttributeValue("domain", "absolute");
        background.SetAttributeValue("filename", AssetReference(path, target, Path.IsPathRooted(filename ?? "")));
    }

    // The file a picture background names from a document at "target" (upstream LoadHandler::setBgPixmap).
    private static string PictureFile(string target, string? domain, string filename) => domain == "attach"
        ? Path.GetFullPath(target + "." + filename)
        : Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(target))!, filename));

    // As upstream SaveHandler + Util::normalizeAssetPath: domain "absolute" just means "a path"; it is relative to the
    // .xopp's folder (upstream's default storage mode) unless the loaded file used an absolute one, with '/'
    // separators. Relative keeps git-synced folders portable between machines.
    private string PdfReference(string pdf, string target) =>
        AssetReference(pdf, target, loadedPdfReference.Filename != null && Path.IsPathRooted(loadedPdfReference.Filename));

    private static string AssetReference(string file, string target, bool absolute)
    {
        string full = Path.GetFullPath(file);
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
                    page.BackgroundCloneSource = pages[index];
                }
                continue;
            }
            string path = PictureFile(referenceBase, domain, filename);
            page.BackgroundImagePath = path; // also when missing (white, as upstream): saving keeps naming it
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
