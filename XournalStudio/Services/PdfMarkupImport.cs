using System.Globalization;
using System.Numerics;
using System.Xml.Linq;
using PdfSharp.Pdf.Content;
using PdfSharp.Pdf.Content.Objects;
using PdfSharp.Pdf;
using PdfSharp.Pdf.Advanced;
using PdfSharp.Pdf.IO;
using Windows.Foundation;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>A picture the import takes from an annotation's appearance: rendered by the caller into <see cref="Element"/>.</summary>
/// <param name="Page">The page in the pictures PDF (the same index as in the source).</param>
/// <param name="Area">The annotation's area on its page (points, top left; as Windows.Data.Pdf measures pages).</param>
/// <param name="Element">The &lt;image&gt; in place (its data still missing).</param>
public sealed record MarkupPicture(int Page, Rect Area, XElement Element);

/// <summary>What <see cref="PdfMarkupImport.Prepare"/> found: per page the new elements in the annotations' order.</summary>
public sealed record MarkupImport(
    IReadOnlyList<(double Width, double Height)> PageSizes,
    IReadOnlyList<IReadOnlyList<XElement>> Elements,
    IReadOnlyList<MarkupPicture> Pictures,
    int Strokes, int Texts, int Kept);

/// <summary>
/// Turns a PDF annotated on the iPad (Apple's markup) into a PDF without that markup and the .xopp elements for it, as
/// an explicit import (Datei → PDF mit iPad-Anmerkungen importieren): handwriting (PencilKit drawings in Apple's
/// <c>AKInkAnnotation2</c>) becomes strokes, text boxes (<c>/FreeText</c>) become texts, every other annotation that
/// shows something (pasted screenshots, stamps, signatures) becomes a picture of its appearance; links, form fields and
/// annotations without appearance stay in the PDF. Nothing shown is lost: a drawing that cannot be read becomes a
/// picture too. The source PDF is never changed.
/// </summary>
public static class PdfMarkupImport
{
    // PencilKit's width is its brush size. Drawn (measured on Apple's appearance images of the user's PDFs, 2026-11-18,
    // ink area over stroke length): the pen 0.4 of it (2.5 → 1.0 pt in eight drawings), the marker about 1.2 of it.
    private const double PenWidthFactor = 0.4, MarkerWidthFactor = 1.2;
    private const byte HighlighterAlpha = 0x7f; // as Xournal stores highlighters
    private const int Hidden = 1 << 1, NoView = 1 << 5;
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    /// <summary>
    /// Writes <paramref name="targetPdf"/> (the source without the taken annotations) and, when pictures are needed,
    /// <paramref name="picturesPdf"/> (the source's pages with only those annotations, for rendering them alone).
    /// </summary>
    public static MarkupImport Prepare(string source, string targetPdf, string picturesPdf)
    {
        using var document = PdfReader.Open(source, PdfDocumentOpenMode.Modify);
        var sizes = new List<(double, double)>();
        var elements = new List<IReadOnlyList<XElement>>();
        var pictures = new List<MarkupPicture>();
        var picturePositions = new List<HashSet<int>>(); // per page: positions in /Annots
        int strokes = 0, texts = 0, kept = 0;
        for (int index = 0; index < document.PageCount; index++)
        {
            var page = document.Pages[index];
            var frame = PageFrame.Of(page);
            sizes.Add(frame.Size);
            var added = new List<XElement>();
            var onPicturesPage = new HashSet<int>();
            var keep = new List<PdfItem>();
            var items = page.Elements.GetArray("/Annots")?.Elements.ToList() ?? [];
            var drawings = items.Select(i => (i is PdfReference r ? r.Value : i) as PdfDictionary)
                .Where(a => a != null && Markup(a) is { ClassName: AppleMarkup.InkClass, Drawing: not null })
                .Select(a => a!.Elements.GetRectangle("/Rect")).ToList();
            for (int position = 0; position < items.Count; position++)
            {
                var item = items[position];
                if ((item is PdfReference r ? r.Value : item) is not PdfDictionary annotation)
                {
                    keep.Add(item);
                    continue;
                }
                if (IsOlderCopyOfDrawing(annotation, drawings))
                {
                    continue; // removed: the drawing comes with its strokes
                }
                switch (Take(annotation, frame, added, out bool picture))
                {
                    case Taken.Strokes:
                        strokes++;
                        break;
                    case Taken.Text:
                        texts++;
                        break;
                    case Taken.Removed:
                        break;
                    case Taken.Kept:
                        keep.Add(item);
                        if (IsMarkup(annotation))
                        {
                            kept++;
                        }
                        break;
                }
                if (picture)
                {
                    var area = frame.Area(annotation.Elements.GetRectangle("/Rect"));
                    var image = new XElement("image");
                    added.Add(image);
                    pictures.Add(new MarkupPicture(index, area, image));
                    onPicturesPage.Add(position);
                }
            }
            SetAnnotations(page, keep);
            elements.Add(added);
            picturePositions.Add(onPicturesPage);
        }
        Save(document, targetPdf);
        if (pictures.Count > 0)
        {
            WritePicturesPdf(source, picturePositions, picturesPdf);
        }
        return new MarkupImport(sizes, elements, pictures, strokes, texts, kept);
    }

    /// <summary>The .xopp document: one page per PDF page on <paramref name="pdf"/>, the import's elements on them.</summary>
    public static XoppDocument CreateDocument(MarkupImport import, string pdf)
    {
        var doc = XoppDocument.FromPdf(pdf, import.PageSizes);
        for (int i = 0; i < doc.Pages.Count; i++)
        {
            doc.Pages[i].AddToTopLayer(import.Elements[i].Where(e => e.Name != "image" || e.Value.Length > 0));
        }
        return doc;
    }

    private enum Taken { Kept, Removed, Strokes, Text }

    private static Taken Take(PdfDictionary annotation, PageFrame frame, List<XElement> added, out bool picture)
    {
        picture = false;
        string subtype = annotation.Elements.GetName("/Subtype");
        if (subtype is "/Link" or "/Widget" or "/Popup")
        {
            return Taken.Kept;
        }
        if (Markup(annotation) is { ClassName: AppleMarkup.InkClass, Drawing: { } drawing } markup)
        {
            try
            {
                var strokes = PencilKitDrawing.Read(drawing);
                added.AddRange(strokes.Select(s => StrokeElement(s, frame, markup)).OfType<XElement>());
                return strokes.Count > 0 ? Taken.Strokes : Shows(annotation) ? Picture(out picture) : Taken.Removed;
            }
            catch (FormatException e)
            {
                DebugLog.Write($"import: drawing of an ink annotation: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}; taken as a picture");
            }
        }
        if (subtype == "/FreeText" && TextElement(annotation, frame) is { } text)
        {
            added.Add(text);
            return Taken.Text;
        }
        return Shows(annotation) ? Picture(out picture) : Taken.Kept;
    }

    // iPadOS (PaperKit) leaves a drawing's earlier appearance as a stamp of its own beside the drawing: no markup archive,
    // only PaperKit ids (/PPKID), the drawing's exact rectangle; its picture is the drawing's (measured on the user's PDFs:
    // pixel for pixel the same). Taken with the drawing, it would lie under its strokes as a stale picture.
    private static bool IsOlderCopyOfDrawing(PdfDictionary annotation, List<PdfRectangle> drawings)
    {
        if (annotation.Elements.GetName("/Subtype") != "/Stamp" ||
            annotation.Elements.GetDictionary("/AAPL:AKExtras") is not { } extras ||
            extras.Elements.ContainsKey("/AAPL:AKAnnotationV2") || extras.Elements.ContainsKey("/AAPL:AKAnnotationObject") ||
            !extras.Elements.ContainsKey("/PPKID"))
        {
            return false;
        }
        var rect = annotation.Elements.GetRectangle("/Rect");
        return drawings.Any(d => Math.Abs(d.X1 - rect.X1) < 0.5 && Math.Abs(d.Y1 - rect.Y1) < 0.5 &&
            Math.Abs(d.X2 - rect.X2) < 0.5 && Math.Abs(d.Y2 - rect.Y2) < 0.5);
    }

    private static Taken Picture(out bool picture)
    {
        picture = true;
        return Taken.Removed;
    }

    private static bool IsMarkup(PdfDictionary annotation) =>
        annotation.Elements.GetName("/Subtype") is not ("/Link" or "/Widget" or "/Popup");

    private static AppleMarkup? Markup(PdfDictionary annotation)
    {
        if (annotation.Elements.GetDictionary("/AAPL:AKExtras") is not { } extras)
        {
            return null;
        }
        foreach (string key in new[] { "/AAPL:AKAnnotationV2", "/AAPL:AKAnnotationObject" })
        {
            if (extras.Elements.GetString(key) is { Length: > 0 } archive)
            {
                try
                {
                    return AppleMarkup.Read(archive);
                }
                catch (FormatException e)
                {
                    DebugLog.Write($"import: {key}: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}");
                }
            }
        }
        return null;
    }

    // Shown by a viewer: not hidden and an appearance with an area (Apple writes empty ones for emptied drawings).
    private static bool Shows(PdfDictionary annotation)
    {
        if ((annotation.Elements.GetInteger("/F") & (Hidden | NoView)) != 0 ||
            annotation.Elements.GetDictionary("/AP")?.Elements.GetDictionary("/N") is not { } normal)
        {
            return false;
        }
        var box = normal.Elements.GetRectangle("/BBox");
        var rect = annotation.Elements.GetRectangle("/Rect");
        return box.Width != 0 && box.Height != 0 && rect.Width != 0 && rect.Height != 0 &&
            double.IsFinite(rect.X1) && Math.Abs(rect.X1) < 1e6 && Math.Abs(rect.Y1) < 1e6;
    }

    private static XElement? StrokeElement(PencilKitStroke stroke, PageFrame frame, AppleMarkup markup)
    {
        // Drawing coordinates span the page (drawingSize = its unrotated size), top left, y down.
        double sx = markup.DrawingWidth > 0 ? frame.Width / markup.DrawingWidth : 1;
        double sy = markup.DrawingHeight > 0 ? frame.Height / markup.DrawingHeight : 1;
        var points = stroke.Points.Select(p => frame.FromTopLeft(p.X * sx, p.Y * sy)).ToList();
        if (points.Count == 0)
        {
            return null;
        }
        if (points.Count == 1)
        {
            points.Add(points[0]); // a dot: Xournal's strokes have two points at least
        }
        bool marker = stroke.IsMarker;
        double width = stroke.Width * Math.Sqrt(sx * sy) * (marker ? MarkerWidthFactor : PenWidthFactor);
        var color = stroke.Color;
        if (marker)
        {
            color.A = HighlighterAlpha;
        }
        return new XElement("stroke",
            new XAttribute("tool", marker ? "highlighter" : "pen"),
            new XAttribute("color", XoppColor.Format(color)),
            new XAttribute("width", Format(Math.Max(width, 0.1))),
            string.Join(' ', points.Select(p => $"{Format(p.X)} {Format(p.Y)}")));
    }

    // A text box: its text where and as large as its appearance draws it (the first line's start: text matrix and font
    // size of the appearance stream, read with PDFsharp's content parser; Apple drew 11 pt for a /DA of 12 pt), else at
    // its rectangle's top left in the /DA's size; the color of its /DA. A .xopp text's y is the top of its first line:
    // the baseline minus the ascent of the font the app shows (Arial, 0.905 em).
    private static XElement? TextElement(PdfDictionary annotation, PageFrame frame)
    {
        string content = annotation.Elements.GetString("/Contents");
        if (string.IsNullOrWhiteSpace(content) || frame.Rotate != 0)
        {
            return null; // nothing to write, or a rotated page: as a picture
        }
        var (size, color) = DefaultAppearance(annotation.Elements.GetString("/DA"));
        var rect = annotation.Elements.GetRectangle("/Rect");
        var area = frame.Area(rect);
        double x = area.X + 2, y = area.Y + 2;
        if (FirstLine(annotation, rect) is { } line)
        {
            const double Ascent = 0.905;
            size = line.Size;
            var start = frame.FromTopLeft(line.X - frame.X1, frame.Y1 + frame.Height - line.Y);
            (x, y) = (start.X, start.Y - Ascent * size);
        }
        return new XElement("text",
            new XAttribute("font", "Sans"),
            new XAttribute("size", Format(size)),
            new XAttribute("x", Format(x)),
            new XAttribute("y", Format(y)),
            new XAttribute("color", XoppColor.Format(color)),
            content.Replace("\r\n", "\n").Replace('\r', '\n'));
    }

    // Where the appearance begins its text (page space) and its font size: the CTM and text matrix at the first text
    // operator, the form mapped onto the annotation's rectangle as a viewer does (BBox → Rect).
    private static (double X, double Y, double Size)? FirstLine(PdfDictionary annotation, PdfRectangle rect)
    {
        if (annotation.Elements.GetDictionary("/AP")?.Elements.GetDictionary("/N") is not { Stream: { } stream } form)
        {
            return null;
        }
        var box = form.Elements.GetRectangle("/BBox");
        if (box.Width == 0 || box.Height == 0)
        {
            return null;
        }
        double sx = Math.Abs(rect.Width / box.Width), sy = Math.Abs(rect.Height / box.Height);
        var ctm = new Matrix3x2((float)sx, 0, 0, (float)sy,
            (float)(Math.Min(rect.X1, rect.X2) - sx * Math.Min(box.X1, box.X2)), (float)(Math.Min(rect.Y1, rect.Y2) - sy * Math.Min(box.Y1, box.Y2)));
        var saved = new Stack<Matrix3x2>();
        var tm = Matrix3x2.Identity;
        double fontSize = 0;
        foreach (var op in ContentReader.ReadContent(stream.UnfilteredValue).OfType<COperator>())
        {
            var n = op.Operands.OfType<CNumber>().Select(c => c switch { CInteger i => (double)i.Value, CReal r => r.Value, _ => 0 }).ToList();
            switch (op.OpCode.Name)
            {
                case "q":
                    saved.Push(ctm);
                    break;
                case "Q" when saved.Count > 0:
                    ctm = saved.Pop();
                    break;
                case "cm" when n.Count == 6:
                    ctm = Matrix(n) * ctm;
                    break;
                case "Tm" when n.Count == 6:
                    tm = Matrix(n);
                    break;
                case "Td" or "TD" when n.Count == 2:
                    tm = Matrix3x2.CreateTranslation((float)n[0], (float)n[1]) * tm;
                    break;
                case "Tf" when n.Count == 1:
                    fontSize = n[0];
                    break;
                case "Tj" or "TJ" or "'" or "\"" when fontSize > 0:
                    var m = tm * ctm;
                    return (m.M31, m.M32, Math.Round(fontSize * Math.Sqrt(Math.Abs(m.GetDeterminant())), 1));
            }
        }
        return null;
    }

    private static Matrix3x2 Matrix(List<double> n) => new((float)n[0], (float)n[1], (float)n[2], (float)n[3], (float)n[4], (float)n[5]);

    // "/Helvetica 12 Tf 0 g" or "... 1 0 0 rg": the font size and the fill color.
    private static (double Size, Windows.UI.Color Color) DefaultAppearance(string da)
    {
        var words = da.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        double size = 12;
        var color = Microsoft.UI.Colors.Black;
        byte Byte(string s) => (byte)Math.Round(Math.Clamp(double.Parse(s, Inv), 0, 1) * 255);
        for (int i = 0; i < words.Length; i++)
        {
            switch (words[i])
            {
                case "Tf" when i >= 1 && double.TryParse(words[i - 1], NumberStyles.Float, Inv, out double s) && s > 0:
                    size = s;
                    break;
                case "g" when i >= 1:
                    byte gray = Byte(words[i - 1]);
                    color = Windows.UI.Color.FromArgb(255, gray, gray, gray);
                    break;
                case "rg" when i >= 3:
                    color = Windows.UI.Color.FromArgb(255, Byte(words[i - 3]), Byte(words[i - 2]), Byte(words[i - 1]));
                    break;
            }
        }
        return (size, color);
    }

    private static void SetAnnotations(PdfPage page, List<PdfItem> keep)
    {
        if (page.Elements.GetArray("/Annots") is not { } annotations || annotations.Elements.Count == keep.Count)
        {
            return;
        }
        if (keep.Count == 0)
        {
            page.Elements.Remove("/Annots");
            return;
        }
        var array = new PdfArray(page.Owner);
        foreach (var item in keep)
        {
            array.Elements.Add(item);
        }
        page.Elements["/Annots"] = array;
    }

    // The source's pages with nothing but the annotations taken as pictures: Windows.Data.Pdf renders them alone.
    private static void WritePicturesPdf(string source, List<HashSet<int>> positions, string target)
    {
        using var document = PdfReader.Open(source, PdfDocumentOpenMode.Modify);
        for (int index = 0; index < document.PageCount; index++)
        {
            var page = document.Pages[index];
            page.Contents.Elements.Clear();
            page.Contents.AppendContent().CreateStream([]);
            // The same annotations in this second reading of the file: by their position in /Annots.
            var keep = page.Elements.GetArray("/Annots")?.Elements.Where((_, i) => positions[index].Contains(i)).ToList() ?? [];
            SetAnnotations(page, keep);
        }
        Save(document, target);
    }

    private static void Save(PdfDocument document, string target)
    {
        string temp = target + "~";
        document.Save(temp);
        File.Move(temp, target, overwrite: true);
    }

    private static string Format(double v) => v.ToString("0.###", Inv);

    /// <summary>A page's visible box (crop box, else media box) and rotation, as Windows.Data.Pdf shows the page.</summary>
    private readonly record struct PageFrame(double X1, double Y1, double Width, double Height, int Rotate)
    {
        public static PageFrame Of(PdfPage page)
        {
            var box = page.Elements.GetRectangle("/CropBox") is { Width: > 0, Height: > 0 } crop ? crop : page.MediaBox;
            int rotate = ((page.Elements.GetInteger("/Rotate") % 360) + 360) % 360;
            return new PageFrame(Math.Min(box.X1, box.X2), Math.Min(box.Y1, box.Y2), Math.Abs(box.Width), Math.Abs(box.Height), rotate);
        }

        public (double Width, double Height) Size => Rotate % 180 == 0 ? (Width, Height) : (Height, Width);

        /// <summary>A point of the unrotated box measured from its top left (y down) on the shown page.</summary>
        public Point FromTopLeft(double u, double v) => Rotate switch
        {
            90 => new Point(Height - v, u),
            180 => new Point(Width - u, Height - v),
            270 => new Point(v, Width - u),
            _ => new Point(u, v),
        };

        /// <summary>A rectangle in PDF user space on the shown page.</summary>
        public Rect Area(PdfRectangle r)
        {
            var a = FromTopLeft(Math.Min(r.X1, r.X2) - X1, Y1 + Height - Math.Min(r.Y1, r.Y2));
            var b = FromTopLeft(Math.Max(r.X1, r.X2) - X1, Y1 + Height - Math.Max(r.Y1, r.Y2));
            return new Rect(a, b);
        }
    }
}
