using System.IO.Compression;
using System.Xml.Linq;
using Claunia.PropertyList;
using Google.Protobuf;
using PdfSharp.Pdf;
using PdfSharp.Pdf.Advanced;
using PdfSharp.Pdf.IO;
using XournalStudio.Model;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

/// <summary>
/// The iPad import: PencilKit drawings (both variants), Apple's markup archive and the import of a PDF built here the
/// way iPadOS writes its markup (layouts as found in the user's PDFs, see PencilKitDrawing).
/// </summary>
public sealed class MarkupImportTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-import-").FullName;

    public void Dispose() => Directory.Delete(temp, true);

    // ------------------------------------------------------------------ protobuf builders

    private static byte[] Message(Action<CodedOutputStream> write)
    {
        using var stream = new MemoryStream();
        var output = new CodedOutputStream(stream);
        write(output);
        output.Flush();
        return stream.ToArray();
    }

    private static void Bytes(CodedOutputStream o, int field, byte[] value)
    {
        o.WriteTag(field, WireFormat.WireType.LengthDelimited);
        o.WriteBytes(ByteString.CopyFrom(value));
    }

    private static void Varint(CodedOutputStream o, int field, ulong value)
    {
        o.WriteTag(field, WireFormat.WireType.Varint);
        o.WriteUInt64(value);
    }

    private static void Float(CodedOutputStream o, int field, float value)
    {
        o.WriteTag(field, WireFormat.WireType.Fixed32);
        o.WriteFloat(value);
    }

    private static byte[] Ink(string id, float r, float g, float b, float a) => Message(o =>
    {
        Bytes(o, 1, Message(c => { Float(c, 1, r); Float(c, 2, g); Float(c, 3, b); Float(c, 4, a); }));
        Bytes(o, 2, System.Text.Encoding.UTF8.GetBytes(id));
    });

    // A path with location (bit 0) and width (bit 2) per point.
    private static byte[] Path((float X, float Y)[] points, float width) => Message(o =>
    {
        Varint(o, 3, (ulong)points.Length);
        Varint(o, 4, 0b101);
        Varint(o, 5, 0xFFF & ~0b101UL);
        // The other channels, constant: time, aspect 1, edge, force 1, azimuth, altitude, opacity 1, radius, threshold, elevation.
        Bytes(o, 6, [
            .. BitConverter.GetBytes(0f), .. BitConverter.GetBytes((ushort)1000), .. BitConverter.GetBytes((ushort)0),
            .. BitConverter.GetBytes((ushort)1000), .. BitConverter.GetBytes((ushort)0), .. BitConverter.GetBytes((ushort)0),
            .. BitConverter.GetBytes((ushort)32768), .. BitConverter.GetBytes(0f), .. BitConverter.GetBytes((ushort)0),
            .. BitConverter.GetBytes(0f)]);
        var records = new List<byte>();
        foreach (var (x, y) in points)
        {
            records.AddRange(BitConverter.GetBytes(x));
            records.AddRange(BitConverter.GetBytes(y));
            records.AddRange(BitConverter.GetBytes(width));
        }
        Bytes(o, 7, records.ToArray());
    });

    private static byte[] Wrd(byte[][] inks, byte[][] strokes)
    {
        var body = Message(o =>
        {
            foreach (var ink in inks)
            {
                Bytes(o, 4, ink);
            }
            foreach (var stroke in strokes)
            {
                Bytes(o, 5, stroke);
            }
        });
        return [(byte)'w', (byte)'r', (byte)'d', 0xf0, 1, 0, .. body];
    }

    private static byte[] Square(params (float X, float Y)[] corners) =>
        corners.SelectMany(c => BitConverter.GetBytes(c.X).Concat(BitConverter.GetBytes(c.Y))).ToArray();

    private static readonly (float, float)[] Line = [(10, 10), (20, 10), (30, 10)];

    private static byte[] SampleDrawing() => Wrd(
        [Ink("com.apple.ink.pen", 0, 0, 1, 1), Ink("com.apple.ink.marker", 1, 1, 0, 0.6f)],
        [
            // plain pen stroke
            Message(o => { Varint(o, 4, 0); Bytes(o, 5, Path(Line, 2.5f)); }),
            // moved marker stroke
            Message(o =>
            {
                Varint(o, 4, 1);
                Bytes(o, 5, Path(Line, 5));
                Bytes(o, 7, Message(t => { Float(t, 1, 1); Float(t, 2, 0); Float(t, 3, 0); Float(t, 4, 1); Float(t, 5, 0); Float(t, 6, 100); }));
            }),
            // partly erased: the range of its first point and a half
            Message(o =>
            {
                Varint(o, 4, 0);
                Bytes(o, 5, Path(Line, 2.5f));
                Bytes(o, 11, Message(p => { Float(p, 12, 0); Float(p, 12, 1.5f); }));
            }),
            // erased: a mask beside all its points
            Message(o =>
            {
                Varint(o, 4, 0);
                Bytes(o, 5, Path(Line, 2.5f));
                Bytes(o, 11, Message(p => Bytes(p, 10, Square((50, 50), (60, 50), (60, 60), (50, 60)))));
            }),
            // clipped by a mask: only x 15..25 stays
            Message(o =>
            {
                Varint(o, 4, 0);
                Bytes(o, 5, Path(Line, 2.5f));
                Bytes(o, 11, Message(p => Bytes(p, 10, Square((15, 0), (25, 0), (25, 20), (15, 20)))));
            }),
            // deleted (no path)
            Message(o => Varint(o, 4, 0)),
        ]);

    [Fact]
    public void PencilKitStrokesWithTransformsErasedRangesAndMasks()
    {
        var strokes = PencilKitDrawing.Read(SampleDrawing());

        Assert.Equal(4, strokes.Count);
        var pen = strokes[0];
        Assert.Equal("com.apple.ink.pen", pen.Ink);
        Assert.False(pen.IsMarker);
        Assert.Equal(2.5, pen.Width, 3);
        Assert.Equal([new(10, 10), new(20, 10), new(30, 10)], pen.Points);
        Assert.Equal(Windows.UI.Color.FromArgb(255, 0, 0, 255), pen.Color);

        var marker = strokes[1];
        Assert.True(marker.IsMarker);
        Assert.Equal(110, marker.Points[0].Y, 3); // moved by its transform
        Assert.Equal(153, marker.Color.A);

        var cut = strokes[2];
        Assert.Equal(10, cut.Points[0].X, 3);
        Assert.Equal(25, cut.Points[^1].X, 3); // parameter 1.5: halfway from the 2nd to the 3rd point

        var clipped = strokes[3];
        Assert.InRange(clipped.Points[0].X, 15, 15.3);
        Assert.InRange(clipped.Points[^1].X, 24.7, 25);
    }

    [Fact]
    public void OlderGzipDrawings()
    {
        byte[] point(float x, float y) =>
        [
            .. BitConverter.GetBytes(0f), .. BitConverter.GetBytes(x), .. BitConverter.GetBytes(y),
            .. BitConverter.GetBytes((ushort)25), .. BitConverter.GetBytes((ushort)1000), .. BitConverter.GetBytes((ushort)0),
            .. BitConverter.GetBytes((ushort)1000), .. BitConverter.GetBytes((ushort)0), .. BitConverter.GetBytes((ushort)0),
        ];
        var drawing = Message(o =>
        {
            Bytes(o, 4, Ink("com.apple.ink.pen", 1, 0, 0, 1));
            Bytes(o, 5, Message(s => { Varint(s, 3, 0); Varint(s, 4, 2); Bytes(s, 5, [.. point(1, 2), .. point(3, 4)]); }));
        });
        var root = Message(o => { Varint(o, 1, 0); Bytes(o, 2, Message(m => Bytes(m, 3, drawing))); });
        using var gzipped = new MemoryStream();
        using (var gzip = new GZipStream(gzipped, CompressionMode.Compress))
        {
            gzip.Write(root);
        }

        var stroke = Assert.Single(PencilKitDrawing.Read(gzipped.ToArray()));
        Assert.Equal([new(1, 2), new(3, 4)], stroke.Points);
        Assert.Equal(2.5, stroke.Width, 3);
    }

    [Fact]
    public void DataThatIsNoDrawingIsAFormatException()
    {
        Assert.Throws<FormatException>(() => PencilKitDrawing.Read([1, 2, 3]));
        Assert.Throws<FormatException>(() => PencilKitDrawing.Read([(byte)'w', (byte)'r', (byte)'d', 0xf0, 1, 0, 0xff, 0xff]));
    }

    // ------------------------------------------------------------------ Apple's markup archive

    private static string Archive(string className, byte[]? drawing)
    {
        var objects = new NSArray(
            new NSString("$null"),
            Root(),
            new NSDictionary { { "$classname", className } },
            new NSData(drawing ?? []),
            new NSDictionary { { "NS.keys", new NSArray(new UID((ulong)5), new UID((ulong)6)) }, { "NS.objects", new NSArray(new UID((ulong)7), new UID((ulong)8)) } },
            new NSString("Width"),
            new NSString("Height"),
            new NSNumber(595.276),
            new NSNumber(841.89));
        var archive = new NSDictionary { { "$archiver", "NSKeyedArchiver" }, { "$top", new NSDictionary { { "root", new UID((ulong)1) } } }, { "$objects", objects }, { "$version", 100000 } };
        return Convert.ToBase64String(BinaryPropertyListWriter.WriteToArray(archive));

        NSDictionary Root()
        {
            var root = new NSDictionary { { "$class", new UID((ulong)2) } };
            if (drawing != null)
            {
                root.Add("drawing", new UID((ulong)3));
                root.Add("drawingSize", new UID((ulong)4));
            }
            return root;
        }
    }

    [Fact]
    public void AppleMarkupNamesItsClassAndDrawing()
    {
        var markup = AppleMarkup.Read(Archive(AppleMarkup.InkClass, [1, 2, 3]));
        Assert.Equal(AppleMarkup.InkClass, markup.ClassName);
        Assert.Equal([1, 2, 3], markup.Drawing);
        Assert.Equal(595.276, markup.DrawingWidth, 3);
        Assert.Equal(841.89, markup.DrawingHeight, 3);
        Assert.Null(AppleMarkup.Read(Archive("AKImageAnnotation", null)).Drawing);
        Assert.Throws<FormatException>(() => AppleMarkup.Read("bm90IGEgcGxpc3Q="));
    }

    // ------------------------------------------------------------------ the import

    private static PdfDictionary Form(PdfDocument pdf, double width, double height, string content)
    {
        var form = new PdfDictionary(pdf);
        form.Elements.SetName("/Type", "/XObject");
        form.Elements.SetName("/Subtype", "/Form");
        form.Elements["/BBox"] = new PdfArray(pdf, new PdfReal(0), new PdfReal(0), new PdfReal(width), new PdfReal(height));
        form.CreateStream(System.Text.Encoding.ASCII.GetBytes(content));
        pdf.Internals.AddObject(form);
        return form;
    }

    private static PdfDictionary Annotation(PdfDocument pdf, string subtype, double x1, double y1, double x2, double y2, PdfDictionary? appearance)
    {
        var a = new PdfDictionary(pdf);
        a.Elements.SetName("/Type", "/Annot");
        a.Elements.SetName("/Subtype", subtype);
        a.Elements["/Rect"] = new PdfArray(pdf, new PdfReal(x1), new PdfReal(y1), new PdfReal(x2), new PdfReal(y2));
        a.Elements.SetInteger("/F", 4);
        if (appearance != null)
        {
            var ap = new PdfDictionary(pdf);
            ap.Elements["/N"] = appearance.Reference;
            a.Elements["/AP"] = ap;
        }
        pdf.Internals.AddObject(a);
        return a;
    }

    private static PdfDictionary Extras(PdfDocument pdf, params (string Key, string Value)[] entries)
    {
        var extras = new PdfDictionary(pdf);
        foreach (var (key, value) in entries)
        {
            extras.Elements.SetString(key, value);
        }
        return extras;
    }

    // An A4 page with: handwriting (an ink annotation with its drawing), the stale copy iPadOS leaves of it, a text box, a
    // stamp with only an appearance, a link.
    private string MarkedUpPdf()
    {
        string path = System.IO.Path.Combine(temp, "ue1.pdf");
        using var pdf = new PdfDocument();
        var page = pdf.AddPage();
        page.Width = PdfSharp.Drawing.XUnit.FromPoint(595.276);
        page.Height = PdfSharp.Drawing.XUnit.FromPoint(841.89);
        var ink = Annotation(pdf, "/Stamp", 0, 700, 100, 841.89, Form(pdf, 100, 141.89, "0 0 1 RG 10 131 m 30 131 l S"));
        ink.Elements["/AAPL:AKExtras"] = Extras(pdf, ("/AAPL:AKAnnotationV2", Archive(AppleMarkup.InkClass, SampleDrawing())));
        var copy = Annotation(pdf, "/Stamp", 0, 700, 100, 841.89, Form(pdf, 100, 141.89, "0 0 1 RG 10 131 m 30 131 l S"));
        copy.Elements["/AAPL:AKExtras"] = Extras(pdf, ("/PPKID", "AkHO"));
        var text = Annotation(pdf, "/FreeText", 248, 405, 378, 424,
            Form(pdf, 130, 19, "BT /F1 1 Tf 11 0 0 11 8.5 5.65 Tm (Name) Tj ET"));
        text.Elements.SetString("/Contents", "Name");
        text.Elements.SetString("/DA", "/Helvetica 12 Tf 0 g");
        Annotation(pdf, "/Stamp", 300, 300, 400, 350, Form(pdf, 100, 50, "0 0 0 rg 0 0 100 50 re f"));
        // a screenshot over it, and a drawing that cannot be read: pictures as well, the overlapping one rendered alone
        Annotation(pdf, "/Stamp", 350, 320, 450, 370, Form(pdf, 100, 50, "1 0 0 rg 0 0 100 50 re f"));
        var damaged = Annotation(pdf, "/Stamp", 100, 100, 200, 150, Form(pdf, 100, 50, "0 0 0 RG 0 0 m 100 50 l S"));
        damaged.Elements["/AAPL:AKExtras"] = Extras(pdf, ("/AAPL:AKAnnotationV2", Archive(AppleMarkup.InkClass, [(byte)'w', (byte)'r', (byte)'d', 0xf0, 1, 0, 0x2a, 0xff])));
        Annotation(pdf, "/Link", 50, 50, 100, 60, null);
        var annotations = new PdfArray(pdf);
        foreach (var o in pdf.Internals.GetAllObjects().OfType<PdfDictionary>().Where(d => d.Elements.GetName("/Type") == "/Annot"))
        {
            annotations.Elements.Add(o.Reference!);
        }
        page.Elements["/Annots"] = annotations;
        pdf.Save(path);
        return path;
    }

    private static List<string> Subtypes(string path)
    {
        using var pdf = PdfReader.Open(path, PdfDocumentOpenMode.Import);
        return pdf.Pages[0].Elements.GetArray("/Annots")?.Elements
            .Select(i => ((PdfDictionary)((PdfReference)i).Value).Elements.GetName("/Subtype")).ToList() ?? [];
    }

    [Fact]
    public void ImportTakesHandwritingTextsAndPicturesOutOfThePdf()
    {
        string source = MarkedUpPdf();
        byte[] before = File.ReadAllBytes(source);
        string target = System.IO.Path.Combine(temp, "out.pdf");
        string Pictures(int round) => System.IO.Path.Combine(temp, $"pictures-{round}.pdf");

        var import = PdfMarkupImport.Prepare(source, target, Pictures);

        Assert.Equal(before, File.ReadAllBytes(source)); // the source stays as it is
        Assert.Equal(["/Link"], Subtypes(target)); // the markup is gone, the link stays
        Assert.Equal(["/Stamp", "/Stamp"], Subtypes(Pictures(0))); // only what becomes a picture, to be rendered alone
        Assert.Equal(["/Stamp"], Subtypes(Pictures(1))); // the one overlapping another, in a pass of its own
        Assert.Equal((595.276, 841.89), (import.PageSizes[0].Width, import.PageSizes[0].Height));
        Assert.Equal(1, import.Drawings);
        Assert.Equal(1, import.Texts);
        Assert.Equal(0, import.Kept);

        var elements = import.Elements[0];
        var strokes = elements.Where(e => e.Name == "stroke").ToList();
        Assert.Equal(4, strokes.Count); // the stale copy brought nothing
        Assert.Equal("pen", (string?)strokes[0].Attribute("tool"));
        Assert.Equal("#0000ffff", (string?)strokes[0].Attribute("color"));
        Assert.Equal("1", (string?)strokes[0].Attribute("width")); // PencilKit's 2.5 as drawn
        Assert.Equal("10 10 20 10 30 10", strokes[0].Value);
        Assert.Equal("highlighter", (string?)strokes[1].Attribute("tool"));
        Assert.Equal("#ffff007f", (string?)strokes[1].Attribute("color"));
        Assert.Equal("6", (string?)strokes[1].Attribute("width"));

        var text = Assert.Single(elements, e => e.Name == "text");
        Assert.Equal("Name", text.Value);
        Assert.Equal("11", (string?)text.Attribute("size")); // as drawn, not the /DA's 12
        Assert.Equal(256.5, double.Parse((string)text.Attribute("x")!, System.Globalization.CultureInfo.InvariantCulture), 1);
        Assert.Equal(841.89 - 410.65 - 0.905 * 11, double.Parse((string)text.Attribute("y")!, System.Globalization.CultureInfo.InvariantCulture), 1);

        Assert.Equal(3, import.Pictures.Count);
        var picture = import.Pictures[0];
        Assert.Equal(new Windows.Foundation.Rect(300, 841.89 - 350, 100, 50), picture.Area);
        Assert.Equal(Pictures(0), picture.Pdf);
        Assert.Equal(Pictures(1), import.Pictures[1].Pdf);
        Assert.Equal(Pictures(0), import.Pictures[2].Pdf); // the unreadable drawing overlaps nothing
        Assert.Contains(picture.Element, elements); // in the annotations' order

        // The document: the PDF as background, the elements on its page (a picture without data is left out).
        var doc = PdfMarkupImport.CreateDocument(import, target);
        string xopp = System.IO.Path.Combine(temp, "out.xopp");
        doc.SaveAsync(xopp).GetAwaiter().GetResult();
        var loaded = XoppDocument.Load(xopp);
        Assert.Equal(target, loaded.PdfPath);
        var page = loaded.Pages.Single().Snapshot();
        Assert.Equal(4, page.Descendants("stroke").Count());
        Assert.Single(page.Descendants("text"));
        Assert.Empty(page.Descendants("image"));
    }
}
