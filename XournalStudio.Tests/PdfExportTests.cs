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
