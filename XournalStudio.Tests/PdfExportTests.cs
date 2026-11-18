using System.IO.Compression;
using PdfSharp.Pdf;
using PdfSharp.Pdf.IO;
using XournalStudio.Model;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

public sealed class PdfExportTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-export-").FullName;

    public void Dispose() => Directory.Delete(temp, true);

    // A 1x1 PNG.
    private const string Png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    private string Xopp(string name, string pages)
    {
        string path = Path.Combine(temp, name);
        using var file = File.Create(path);
        using var gzip = new GZipStream(file, CompressionMode.Compress);
        using var writer = new StreamWriter(gzip);
        writer.Write($"""<?xml version="1.0" standalone="no"?><xournal creator="t" fileversion="4"><title>t</title>{pages}</xournal>""");
        return path;
    }

    private string LecturePdf(int pages)
    {
        string path = Path.Combine(temp, "lecture.pdf");
        using var pdf = new PdfDocument();
        for (int i = 0; i < pages; i++)
        {
            pdf.AddPage();
        }
        pdf.Save(path);
        return path;
    }

    [Fact]
    public void ExportsEveryPageWithItsBackgroundStrokesAndPictures()
    {
        string pdf = LecturePdf(2);
        string doc = Xopp("a.xopp", $"""
            <page width="595.27559" height="841.88976"><background type="pdf" domain="absolute" filename="lecture.pdf" pageno="2"/>
              <layer><stroke tool="pen" color="#3333ccff" width="1.41 1.1 1.3">10 10 50 50 90 10</stroke>
              <stroke tool="highlighter" color="#ffff007f" width="8.5">10 100 200 100</stroke></layer></page>
            <page width="595.27559" height="841.88976"><background type="solid" color="#ffffffff" style="graph"/>
              <layer><image left="10" top="10" right="110" bottom="60">{Png}</image><text font="Sans" size="12" x="1" y="1">Hallo</text></layer></page>
            """);
        var document = XoppDocument.Load(doc);
        string target = Path.Combine(temp, "a.pdf");

        var result = PdfExport.Write(document.Snapshot(doc), document.PdfPath, target);

        Assert.Equal(2, result.Pages);
        Assert.Equal(0, result.SkippedElements); // the <text> is drawn too
        using var exported = PdfReader.Open(target, PdfDocumentOpenMode.Import);
        Assert.Equal(2, exported.PageCount);
        Assert.Equal(595.27559, exported.Pages[0].Width.Point, 3);
        Assert.False(File.Exists(target + "~"));
    }

    [Fact]
    public void TheBackgroundPdfIsNeverOverwritten()
    {
        string pdf = LecturePdf(1);
        string doc = Xopp("b.xopp", """<page width="595" height="842"><background type="pdf" domain="absolute" filename="lecture.pdf" pageno="1"/><layer/></page>""");
        var document = XoppDocument.Load(doc);
        long before = new FileInfo(pdf).Length;

        Assert.Throws<IOException>(() => PdfExport.Write(document.Snapshot(doc), document.PdfPath, pdf));
        Assert.Equal(before, new FileInfo(pdf).Length);
    }

    [Fact]
    public void StrokeShapeHasTheWidthOfEachSegment()
    {
        var shape = StrokeConverter.ReadShape(System.Xml.Linq.XElement.Parse(
            """<stroke tool="pen" color="#ff0000ff" width="2 1.5 2.5">0 0 10 0 20 0</stroke>"""))!;
        Assert.Equal([1.5, 2.5], shape.SegmentWidths);
        Assert.False(shape.EvenWidth);
        Assert.True(StrokeConverter.ReadShape(System.Xml.Linq.XElement.Parse(
            """<stroke tool="highlighter" color="#ffff007f" width="8.5">0 0 10 0</stroke>"""))!.EvenWidth);
    }
    // A page with a signature as Apple's Preview writes it (a Stamp annotation with an appearance stream) and a hidden one.
    private string SignedPdf(string? ownerPassword = null)
    {
        string path = Path.Combine(temp, "signed.pdf");
        using var pdf = new PdfDocument();
        var page = pdf.AddPage();
        var annotations = new PdfArray(pdf);
        // Printable; without flags (shown on screen too); hidden. The second's rectangle is given by its other corners.
        foreach (var (drawing, flags, rect) in new[]
        {
            ("3.21 4.32 m 9.87 6.54 l S", 4, new[] { 100.0, 100, 140, 120 }),
            ("5.55 6.66 m 4.44 3.33 l S", 0, new[] { 240.0, 220, 200, 200 }),
            ("1.11 2.22 m 7.77 8.88 l S", 4 | 2, new[] { 100.0, 100, 140, 120 }),
        })
        {
            var appearance = new PdfDictionary(pdf);
            appearance.Elements.SetName("/Type", "/XObject");
            appearance.Elements.SetName("/Subtype", "/Form");
            appearance.Elements["/BBox"] = new PdfArray(pdf, new PdfReal(0), new PdfReal(0), new PdfReal(20), new PdfReal(10));
            appearance.CreateStream(System.Text.Encoding.ASCII.GetBytes(drawing));
            pdf.Internals.AddObject(appearance);
            var ap = new PdfDictionary(pdf);
            ap.Elements["/N"] = appearance.Reference;
            var stamp = new PdfDictionary(pdf);
            stamp.Elements.SetName("/Type", "/Annot");
            stamp.Elements.SetName("/Subtype", "/Stamp");
            stamp.Elements["/Rect"] = new PdfArray(pdf, rect.Select(v => (PdfItem)new PdfReal(v)).ToArray());
            if (flags != 0)
            {
                stamp.Elements.SetInteger("/F", flags);
            }
            stamp.Elements["/AP"] = ap;
            pdf.Internals.AddObject(stamp);
            annotations.Elements.Add(stamp.Reference!);
        }
        page.Elements["/Annots"] = annotations;
        if (ownerPassword != null)
        {
            pdf.SecuritySettings.OwnerPassword = ownerPassword; // opens without a password, changes need it
        }
        pdf.Save(path);
        return path;
    }

    private static string AllStreams(string path)
    {
        using var pdf = PdfReader.Open(path, PdfDocumentOpenMode.Import);
        return string.Join("\n", pdf.Internals.GetAllObjects().OfType<PdfDictionary>()
            .Where(d => d.Stream != null).Select(d => System.Text.Encoding.ASCII.GetString(d.Stream.UnfilteredValue)));
    }

    [Fact]
    public void AnnotationsOfTheBackgroundPdfAreExportedAsTheyAreShown()
    {
        SignedPdf();
        string doc = Xopp("s.xopp", """<page width="595" height="842"><background type="pdf" domain="absolute" filename="signed.pdf" pageno="1"/><layer/></page>""");
        var document = XoppDocument.Load(doc);
        string target = Path.Combine(temp, "s-export.pdf");

        PdfExport.Write(document.Snapshot(doc), document.PdfPath, target);

        string streams = AllStreams(target);
        Assert.Contains("9.87 6.54 l S", streams); // the printable one, drawn at its rectangle
        Assert.Contains("2 0 0 2 100 100 cm", streams);
        Assert.Contains("5.55 6.66 m 4.44 3.33 l S", streams); // no flags: shown, so exported
        Assert.Contains("2 0 0 2 200 200 cm", streams); // its rectangle normalized, not mirrored
        Assert.DoesNotContain("7.77 8.88 l S", streams); // the hidden one is not
    }

    [Fact]
    public void APdfThatCannotBeChangedIsExportedWithoutItsAnnotationsAndSaysSo()
    {
        SignedPdf(ownerPassword: "owner");
        string doc = Xopp("p.xopp", """<page width="595" height="842"><background type="pdf" domain="absolute" filename="signed.pdf" pageno="1"/><layer/></page>""");
        var document = XoppDocument.Load(doc);
        string target = Path.Combine(temp, "p-export.pdf");

        var result = PdfExport.Write(document.Snapshot(doc), document.PdfPath, target);

        Assert.True(result.AnnotationsLeftOut);
        Assert.Equal(0, result.MissingBackgrounds);
        Assert.Equal(1, PdfReader.Open(target, PdfDocumentOpenMode.Import).PageCount);
    }

    [Fact]
    public void UnknownFontsUnreadablePictureBackgroundsAndOddStrokesDoNotStopTheExport()
    {
        string doc = Xopp("odd.xopp", """
            <page width="100" height="100"><background type="pixmap" domain="absolute" filename="broken.png"/><layer>
            <text font="Cantarell Bold" size="10" x="10" y="10" color="#000000ff">Linux</text>
            <stroke tool="pen" color="#000000ff" width="1 0.5 2 1" style="dash">0 50 1 50 2 50 3 50 90 50</stroke>
            <stroke tool="pen" color="#000000ff" width="0" style="dot">0 60 90 60</stroke>
            </layer></page>
            """);
        File.WriteAllText(Path.Combine(temp, "broken.png"), "not a picture");
        var document = XoppDocument.Load(doc);
        string target = Path.Combine(temp, "odd.pdf");
        var result = PdfExport.Write(document.Snapshot(doc), null, target, i => document.Pages[i].BackgroundImagePath);
        Assert.Equal(1, result.Pages);
        Assert.Equal(1, result.MissingBackgrounds);
        Assert.Equal(1, PdfReader.Open(target, PdfDocumentOpenMode.Import).PageCount);
    }
}
