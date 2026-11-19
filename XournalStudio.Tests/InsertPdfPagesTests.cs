using PdfSharp.Pdf;
using PdfSharp.Pdf.Advanced;
using PdfSharp.Pdf.IO;
using XournalStudio.Model;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

/// <summary>Pages of another PDF appended to the document's PDF as upstream stores it.</summary>
public sealed class InsertPdfPagesTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-insert-").FullName;

    public void Dispose() => Directory.Delete(temp, true);

    // A PDF of pages with these widths (to recognize them), the first with a signature annotation as iPadOS writes it.
    private string Pdf(string name, double[] widths, bool signed = false)
    {
        string path = Path.Combine(temp, name);
        using var pdf = new PdfDocument();
        foreach (double width in widths)
        {
            var page = pdf.AddPage();
            page.Width = PdfSharp.Drawing.XUnit.FromPoint(width);
            page.Height = PdfSharp.Drawing.XUnit.FromPoint(842);
        }
        if (signed)
        {
            var form = new PdfDictionary(pdf);
            form.Elements.SetName("/Type", "/XObject");
            form.Elements.SetName("/Subtype", "/Form");
            form.Elements["/BBox"] = new PdfArray(pdf, new PdfReal(0), new PdfReal(0), new PdfReal(20), new PdfReal(10));
            form.CreateStream(System.Text.Encoding.ASCII.GetBytes("1.23 4.56 m 7.89 6.54 l S"));
            pdf.Internals.AddObject(form);
            var ap = new PdfDictionary(pdf);
            ap.Elements["/N"] = form.Reference;
            var stamp = new PdfDictionary(pdf);
            stamp.Elements.SetName("/Type", "/Annot");
            stamp.Elements.SetName("/Subtype", "/Stamp");
            stamp.Elements["/Rect"] = new PdfArray(pdf, new PdfReal(100), new PdfReal(100), new PdfReal(140), new PdfReal(120));
            stamp.Elements.SetInteger("/F", 4);
            stamp.Elements["/AP"] = ap;
            pdf.Internals.AddObject(stamp);
            pdf.Pages[0].Elements["/Annots"] = new PdfArray(pdf, stamp.Reference!);
        }
        pdf.Save(path);
        return path;
    }

    private static List<double> Widths(string path)
    {
        using var pdf = PdfReader.Open(path, PdfDocumentOpenMode.Import);
        return pdf.Pages.Cast<PdfPage>().Select(p => Math.Round(p.Width.Point)).ToList();
    }

    [Fact]
    public void PagesAreAppendedSoThePagesThereKeepTheirNumbers()
    {
        string target = Pdf("angabe.pdf", [500, 501]);
        string source = Pdf("deckblatt.pdf", [600, 601, 602]);
        byte[] before = File.ReadAllBytes(source);

        int first = PdfPageAppender.Append(target, source, [2, 0]);

        Assert.Equal(2, first);
        Assert.Equal([500, 501, 602, 600], Widths(target));
        Assert.Equal(before, File.ReadAllBytes(source)); // the other PDF stays as it is
        Assert.False(File.Exists(target + "~"));
    }

    [Fact]
    public void WhatAnnotationsShowIsDrawnIntoTheAppendedPages()
    {
        string target = Pdf("angabe.pdf", [500]);
        string source = Pdf("deckblatt.pdf", [600], signed: true);

        PdfPageAppender.Append(target, source, [0]);

        using var pdf = PdfReader.Open(target, PdfDocumentOpenMode.Import);
        var page = pdf.Pages[1];
        Assert.Null(page.Elements.GetArray("/Annots")); // drawn into the page, not shown twice
        string streams = string.Join("\n", pdf.Internals.GetAllObjects().OfType<PdfDictionary>()
            .Where(d => d.Stream != null).Select(d => System.Text.Encoding.ASCII.GetString(d.Stream.UnfilteredValue)));
        Assert.Contains("7.89 6.54 l S", streams); // the signature's appearance is in the PDF
    }

    [Fact]
    public async Task PagesOfAnotherDocumentAreCopiedWithEverythingAndReferToTheTargetsPdfPages()
    {
        string pdf = Pdf("skript.pdf", [500, 510, 520]);
        var source = XoppDocument.FromPdf(pdf, [(500, 842), (510, 842), (520, 842)]);
        source.Pages[2].AddToTopLayer([new System.Xml.Linq.XElement("text", new System.Xml.Linq.XAttribute("unknown", "kept"), "Hallo")]);
        string sourcePath = Path.Combine(temp, "quelle.xopp");
        await source.SaveAsync(sourcePath);
        var loaded = XoppDocument.Load(sourcePath);

        Assert.Equal([0, 2], loaded.PdfPagesOf([2, 0]));
        var copies = XoppDocument.CopyPages(loaded, [2, 0], p => p + 10); // appended at page 11 of the target's PDF

        Assert.Equal([12, 10], copies.Select(c => c.PdfPageIndex));
        Assert.All(copies, c => Assert.Null(c.Background.Attribute("filename"))); // only the target's first PDF page names it
        Assert.Equal("Hallo", copies[0].Texts.Single().Value);
        Assert.Equal("kept", (string?)copies[0].Texts.Single().Attribute("unknown")); // nothing is lost on the way
        Assert.Equal(2, loaded.Pages[2].PdfPageIndex); // the source is untouched
        Assert.NotNull(loaded.Pages[0].Background.Attribute("filename"));
    }
}
