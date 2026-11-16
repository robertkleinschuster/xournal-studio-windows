using System.IO.Compression;
using System.Xml.Linq;
using XournalStudio.Model;
using Xunit;

namespace XournalStudio.Tests;

public class FileFormatTests : IDisposable
{
    private readonly string dir = Path.Combine(Path.GetTempPath(), "xs-tests-" + Guid.NewGuid().ToString("N"));

    public FileFormatTests() => Directory.CreateDirectory(dir);

    public void Dispose() => Directory.Delete(dir, true);

    private string Write(string name, string xml, bool gzip = true)
    {
        string path = Path.Combine(dir, name);
        using var file = File.Create(path);
        using Stream output = gzip ? new GZipStream(file, CompressionMode.Compress) : file;
        using var writer = new StreamWriter(output);
        writer.Write(xml);
        return path;
    }

    private static XDocument ReadXml(string path)
    {
        using var file = File.OpenRead(path);
        using var gzip = new GZipStream(file, CompressionMode.Decompress);
        return XDocument.Load(gzip);
    }

    private const string Sample = """
        <?xml version="1.0" standalone="no"?>
        <xournal creator="xournalpp 1.3.8+dev" fileversion="5" fork-export="x_kommentiert.pdf">
        <title>Xournal++ document - see https://xournalpp.github.io/</title>
        <page width="595.27559" height="841.88976">
        <background type="pdf" domain="absolute" filename="lecture.pdf" pageno="1"/>
        <layer>
        <stroke tool="pen" ts="0" fn="" color="#3333ccff" width="1.41 1.1 1.2" capStyle="round">10 20 30 40 50 60</stroke>
        <text font="Sans" size="12" x="10" y="10" color="#000000ff">Hallo</text>
        </layer>
        </page>
        <page width="595.27559" height="841.88976">
        <background type="pdf" pageno="2"/>
        <layer name="Ebene 2"><stroke tool="highlighter" color="#ffff007f" width="8.5">1 2 3 4</stroke></layer>
        </page>
        <page width="595.27559" height="841.88976">
        <background type="solid" color="#ffffffff" style="graph"/>
        <layer/>
        </page>
        </xournal>
        """;

    [Fact]
    public void LoadsPagesBackgroundsAndPdfReference()
    {
        File.WriteAllText(Path.Combine(dir, "lecture.pdf"), "");
        var doc = XoppDocument.Load(Write("a.xopp", Sample));

        Assert.Equal(3, doc.Pages.Count);
        Assert.True(doc.Pages[0].IsPdf);
        Assert.Equal(1, doc.Pages[1].PdfPageIndex);
        Assert.Equal(BackgroundStyle.Graph, doc.Pages[2].Style);
        Assert.Equal(Path.Combine(dir, "lecture.pdf"), doc.PdfPath);
        Assert.Single(doc.Pages[0].Strokes);
        Assert.True(doc.Pages[1].Strokes[0].DrawingAttributes.DrawAsHighlighter);
    }

    [Fact]
    public void UnchangedDocumentIsWrittenBackWithEverythingItContains()
    {
        string path = Write("b.xopp", Sample);
        var before = ReadXml(path);
        XoppDocument.Load(path).Save(path);
        var after = ReadXml(path);

        Assert.Equal("5", (string?)after.Root!.Attribute("fileversion"));
        // Nothing is removed, also not attributes of other programs.
        Assert.Equal("x_kommentiert.pdf", (string?)after.Root.Attribute("fork-export"));
        Assert.Equal("Xournal Studio for Windows", (string?)after.Root.Attribute("creator"));
        var pagesBefore = before.Root!.Elements("page").ToList();
        var pagesAfter = after.Root.Elements("page").ToList();
        Assert.Equal(pagesBefore.Count, pagesAfter.Count);
        for (int i = 0; i < pagesBefore.Count; i++)
        {
            Assert.True(XNode.DeepEquals(Normalize(pagesBefore[i]), Normalize(pagesAfter[i])), $"page {i + 1}");
        }
        Assert.False(File.Exists(path + "~"));
    }

    [Fact]
    public void SavingKeepsAttributesOfOtherPrograms()
    {
        string path = Write("d.xopp", """
            <xournal creator="x" fileversion="4" fork-export="e.pdf"><title>t</title>
            <page width="100" height="100" fork-page="1"><background type="solid" color="#ffffffff" style="graph"/>
            <layer name="L"><teximage text="" left="1" top="2" right="3" bottom="4" portal="abc" portal-area="1 2 3 4">JVBERi0=</teximage>
            <stroke tool="pen" color="#000000ff" width="1" capStyle="round" ts="0" fn="" extra="x">1 2 3 4</stroke></layer></page></xournal>
            """);
        XoppDocument.Load(path).Save(path);
        var root = ReadXml(path).Root!;
        Assert.Equal("e.pdf", (string?)root.Attribute("fork-export"));
        Assert.Equal("1", (string?)root.Element("page")!.Attribute("fork-page"));
        Assert.Equal("L", (string?)root.Descendants("layer").Single().Attribute("name"));
        var tex = root.Descendants("teximage").Single();
        Assert.Equal("abc", (string?)tex.Attribute("portal"));
        Assert.Equal("JVBERi0=", tex.Value);
        var stroke = root.Descendants("stroke").Single();
        Assert.Equal("x", (string?)stroke.Attribute("extra"));
        Assert.Equal("round", (string?)stroke.Attribute("capStyle"));
    }

    [Fact]
    public void ReadsPlainXmlToo()
    {
        var doc = XoppDocument.Load(Write("c.xopp", Sample, gzip: false));
        Assert.Equal(3, doc.Pages.Count);
    }

    [Fact]
    public void StrokeRoundTripKeepsPointsColorAndWidths()
    {
        var e = XElement.Parse("""<stroke tool="pen" color="#ff0000ff" width="2 1.5 2.5 3">1.5 2.5 10 20 30 40 50 60</stroke>""");
        var stroke = StrokeConverter.FromXml(e)!;
        var back = StrokeConverter.ToXml(stroke);

        Assert.Equal("pen", (string?)back.Attribute("tool"));
        Assert.Equal("#ff0000ff", (string?)back.Attribute("color"));
        Assert.Equal("1.5 2.5 10 20 30 40 50 60", back.Value);
        var widths = ((string)back.Attribute("width")!).Split(" ").Select(s => double.Parse(s, System.Globalization.CultureInfo.InvariantCulture)).ToArray();
        Assert.Equal(new[] { 2, 1.5, 2.5, 3 }, widths.Select(w => Math.Round(w, 2)));
    }

    [Fact]
    public void StrokesAreDrawnAsPolylines()
    {
        // A rectangle (resistor) of five points must keep its corners, like in Xournal++.
        var e = XElement.Parse("""<stroke tool="pen" color="#000000ff" width="1.41">10 10 50 10 50 30 10 30 10 10</stroke>""");
        var stroke = StrokeConverter.FromXml(e)!;
        Assert.False(stroke.DrawingAttributes.FitToCurve);
        Assert.Equal(5, stroke.GetInkPoints().Count);
    }

    [Fact]
    public void HighlighterKeepsXournalAlpha()
    {
        var e = XElement.Parse("""<stroke tool="highlighter" color="#ffff007f" width="8.5">1 2 3 4</stroke>""");
        var back = StrokeConverter.ToXml(StrokeConverter.FromXml(e)!);
        Assert.Equal("highlighter", (string?)back.Attribute("tool"));
        Assert.Equal("#ffff007f", (string?)back.Attribute("color"));
        Assert.Equal("8.5", (string?)back.Attribute("width"));
    }

    [Fact]
    public void UnsavedDocumentFollowsItsRenamedPdf()
    {
        Directory.CreateDirectory(dir);
        var doc = XoppDocument.FromPdf(Path.Combine(dir, "alt.pdf"), [(595, 842)]);
        doc.MovePdf(Path.Combine(dir, "neu.pdf"));
        string path = Path.Combine(dir, "neu.xopp");
        doc.Save(path);

        Assert.Equal(Path.Combine(dir, "neu.pdf"), XoppDocument.Load(path).PdfPath);
    }

    [Fact]
    public void LoadedDocumentFollowsItsRenamedPdf()
    {
        File.WriteAllText(Path.Combine(dir, "lecture.pdf"), "");
        string path = Write("a.xopp", Sample);
        var doc = XoppDocument.Load(path);
        File.Move(Path.Combine(dir, "lecture.pdf"), Path.Combine(dir, "Skript.pdf"));
        doc.MovePdf(Path.Combine(dir, "Skript.pdf"));
        doc.Save(path);

        Assert.Equal("Skript.pdf", PdfFilename(path));
    }

    private static string? PdfFilename(string path) =>
        (string?)ReadXml(path).Descendants("background").First().Attribute("filename");

    [Fact]
    public void NewDocumentRefersToItsPdfRelatively()
    {
        Directory.CreateDirectory(Path.Combine(dir, "VO"));
        var doc = XoppDocument.FromPdf(Path.Combine(dir, "VO", "Skript 1.pdf"), [(595, 842)]);
        string path = Path.Combine(dir, "a.xopp");
        doc.Save(path);

        Assert.Equal("VO/Skript 1.pdf", PdfFilename(path));
        Assert.Equal(Path.Combine(dir, "VO", "Skript 1.pdf"), XoppDocument.Load(path).PdfPath);
    }

    [Fact]
    public void LoadedRelativeReferenceIsKeptAndFollowsASavedCopy()
    {
        File.WriteAllText(Path.Combine(dir, "lecture.pdf"), "");
        string path = Write("a.xopp", Sample);
        var doc = XoppDocument.Load(path);
        doc.Save(path);
        Assert.Equal("lecture.pdf", PdfFilename(path));

        Directory.CreateDirectory(Path.Combine(dir, "Kopie"));
        string copy = Path.Combine(dir, "Kopie", "a.xopp");
        doc.Save(copy);
        Assert.Equal("../lecture.pdf", PdfFilename(copy));
        Assert.Equal(Path.Combine(dir, "lecture.pdf"), XoppDocument.Load(copy).PdfPath);
    }

    [Fact]
    public void LoadedAbsoluteReferenceIsRepairedAsAbsolute()
    {
        // Written on another machine; the PDF is found next to the .xopp.
        string pdf = Path.Combine(dir, "lecture.pdf");
        File.WriteAllText(pdf, "");
        string path = Write("a.xopp", Sample.Replace("filename=\"lecture.pdf\"", "filename=\"Z:/elsewhere/lecture.pdf\""));
        var doc = XoppDocument.Load(path);
        doc.Save(path);

        Assert.Equal(pdf.Replace('\\', '/'), PdfFilename(path));
    }

    [Fact]
    public void AttachedPdfIsReferencedByPathFromAnotherFile()
    {
        // "attach" names "<document>.xopp.bg.pdf": a copy under another name must name the real file instead.
        string path = Write("a.xopp", Sample.Replace("domain=\"absolute\" filename=\"lecture.pdf\"", "domain=\"attach\" filename=\"bg.pdf\""));
        File.WriteAllText(path + ".bg.pdf", "");
        var doc = XoppDocument.Load(path);
        doc.Save(path);
        Assert.Equal("bg.pdf", PdfFilename(path));

        string copy = Path.Combine(dir, "copy.xopp");
        XoppDocument.Write(doc.Snapshot(copy), copy);
        Assert.Equal("a.xopp.bg.pdf", PdfFilename(copy));
        Assert.Equal(path + ".bg.pdf", XoppDocument.Load(copy).PdfPath);
    }

    [Fact]
    public void TextIsReadAsUpstreamWritesIt()
    {
        var legacy = XoppText.Read(XElement.Parse("""
            <text font="Sans Bold" size="14" x="10" y="20" color="#ff0000ff" wrap="150" align="center">Zeile 1
            Zeile 2</text>
            """));
        Assert.Equal("Sans", legacy.Family);
        Assert.Equal("Arial", legacy.WindowsFamily);
        Assert.True(legacy.Bold);
        Assert.Equal(14, legacy.Size);
        Assert.Equal(150, legacy.Wrap);
        Assert.Equal(XoppTextAlignment.Center, legacy.Alignment);
        Assert.Equal(new System.Numerics.Vector2(10, 20), legacy.Transform.Translation);
        Assert.Equal("Zeile 1\nZeile 2", legacy.Content.Replace("\r", ""));

        var placed = XoppText.Read(XElement.Parse("""<text font="Serif" size="12" matrix="0 1 -1 0 50 60" color="#000000ff">x</text>"""));
        Assert.Equal(new System.Numerics.Matrix3x2(0, 1, -1, 0, 50, 60), placed.Transform); // turned by 90°
        Assert.Null(placed.Wrap);
    }

    [Fact]
    public void FilledAndDashedStrokesAreKeptAndDrawnFromTheFile()
    {
        var page = XoppPage.FromXml(XElement.Parse("""
            <page width="100" height="100"><background type="solid" color="#ffffffff" style="plain"/><layer>
            <stroke tool="pen" color="#ff0000ff" width="2" fill="128">0 0 50 0 25 40 0 0</stroke>
            <stroke tool="pen" color="#000000ff" width="1" style="cust: 2 1.5">0 50 90 50</stroke>
            <stroke tool="pen" color="#000000ff" width="1" capStyle="butt">0 60 90 60</stroke>
            <stroke tool="pen" color="#000000ff" width="1" capStyle="round">0 70 90 70</stroke>
            </layer></page>
            """));
        Assert.Single(page.Strokes); // only the plain one is ink
        Assert.Equal(3, page.DrawnStrokes.Count());
        Assert.Equal([2, 1.5], StrokeConverter.Dashes("cust: 2 1.5"));
        Assert.Equal([6, 3, 0.5, 3], StrokeConverter.Dashes("dashdot"));
        var filled = StrokeConverter.ReadShape(page.DrawnStrokes.First())!;
        Assert.Equal(128, filled.Fill);
        Assert.Equal(3, page.Snapshot().Elements("layer").Single().Elements("stroke").Count(e => e.Attribute("fill") != null || e.Attribute("style") != null || (string?)e.Attribute("capStyle") == "butt"));
    }

    [Fact]
    public void PictureBackgroundsStillNameTheirFilesAfterSaveAsAndInsertedPages()
    {
        string path = Write("a.xopp", """
            <xournal fileversion="4"><title>t</title>
            <page width="100" height="100"><background type="pixmap" domain="attach" filename="bg_1.png"/><layer/></page>
            <page width="100" height="100"><background type="pixmap" domain="absolute" filename="pics/p.png"/><layer/></page>
            <page width="100" height="100"><background type="pixmap" domain="clone" filename="0"/><layer/></page>
            </xournal>
            """);
        File.WriteAllBytes(path + ".bg_1.png", [1]);
        Directory.CreateDirectory(Path.Combine(dir, "pics"));
        File.WriteAllBytes(Path.Combine(dir, "pics", "p.png"), [1]);
        var doc = XoppDocument.Load(path);
        Assert.Equal(path + ".bg_1.png", doc.Pages[2].BackgroundImagePath);

        // Saved where it was: written back as it was.
        var same = doc.Snapshot(path).Root!.Elements("page").Select(p => p.Element("background")!).ToList();
        Assert.Equal(("attach", "bg_1.png"), ((string?)same[0].Attribute("domain"), (string?)same[0].Attribute("filename")));
        Assert.Equal("pics/p.png", (string?)same[1].Attribute("filename"));

        // Save As in a subfolder with a page inserted first: paths from there, the clone's index follows its page.
        doc.Pages.Insert(0, XoppPage.CreateGraph(100, 100));
        var moved = doc.Snapshot(Path.Combine(dir, "sub", "b.xopp")).Root!.Elements("page").Select(p => p.Element("background")!).ToList();
        Assert.Equal(("absolute", "../a.xopp.bg_1.png"), ((string?)moved[1].Attribute("domain"), (string?)moved[1].Attribute("filename")));
        Assert.Equal("../pics/p.png", (string?)moved[2].Attribute("filename"));
        Assert.Equal(("clone", "1"), ((string?)moved[3].Attribute("domain"), (string?)moved[3].Attribute("filename")));

        // A missing picture keeps being named (from the new place too); a clone whose page is gone names the picture.
        File.Delete(Path.Combine(dir, "pics", "p.png"));
        doc = XoppDocument.Load(path);
        doc.Pages.RemoveAt(0);
        var after = doc.Snapshot(Path.Combine(dir, "sub", "c.xopp")).Root!.Elements("page").Select(p => p.Element("background")!).ToList();
        Assert.Equal("../pics/p.png", (string?)after[0].Attribute("filename"));
        Assert.Equal(("absolute", "../a.xopp.bg_1.png"), ((string?)after[1].Attribute("domain"), (string?)after[1].Attribute("filename")));
    }

    [Fact]
    public void PastedContentGoesBelowTextsAndDrawnStrokes()
    {
        var page = XoppPage.FromXml(XElement.Parse("""
            <page width="200" height="300"><background type="solid" color="#ffffffff" style="plain"/><layer>
            <stroke tool="pen" color="#000000ff" width="2" style="dash">0 50 90 50</stroke>
            <text font="Sans" size="10" x="10" y="100" color="#000000ff">a
            b</text>
            </layer></page>
            """));
        Assert.Equal(124, page.ContentBottom(), 3); // two lines of 1.2 × 10 below y = 100
        Assert.True(XoppText.Read(XElement.Parse("""<text font="Sans oblique" size="10" x="0" y="0">x</text>""")).Italic);
        var ruling = BackgroundRuling.For(BackgroundStyle.Ruled, "f1=0xff0000", Windows.UI.Color.FromArgb(255, 255, 255, 255), 100, 300);
        Assert.Equal(Windows.UI.Color.FromArgb(255, 255, 0, 0), ruling[0].Color);
    }

    // Whitespace-only text nodes depend on formatting, not content.
    private static XElement Normalize(XElement e)
    {
        var copy = new XElement(e);
        foreach (var text in copy.DescendantNodes().OfType<XText>().Where(t => string.IsNullOrWhiteSpace(t.Value)).ToList())
        {
            text.Remove();
        }
        return copy;
    }
}
