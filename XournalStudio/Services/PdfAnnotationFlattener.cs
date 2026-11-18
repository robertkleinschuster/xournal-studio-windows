using System.Globalization;
using System.Text;
using PdfSharp.Pdf;
using PdfSharp.Pdf.Advanced;
using PdfSharp.Pdf.IO;

namespace XournalStudio.Services;

/// <summary>
/// Puts what a PDF's annotations show into its pages' content, as printing does (ISO 32000-1, 12.5.5): annotations
/// (signatures and texts added in Apple's Preview are Stamp and FreeText annotations, comments, ink, form fields) are not
/// part of a page's content, so a page imported as a form (<see cref="PdfSharp.Drawing.XPdfForm"/>) leaves them out.
/// Each printable, visible annotation's normal appearance stream is drawn at its rectangle on top of the page's content,
/// as a viewer draws it.
/// </summary>
public static class PdfAnnotationFlattener
{
    private const int Hidden = 1 << 1, Print = 1 << 2;

    /// <summary>
    /// The PDF with its annotations drawn into the pages, or null when no page has an annotation to draw (then the file is
    /// used as it is).
    /// </summary>
    public static MemoryStream? Flatten(string path)
    {
        using var document = PdfReader.Open(path, PdfDocumentOpenMode.Modify);
        int drawn = 0;
        foreach (var page in document.Pages)
        {
            drawn += FlattenPage(document, page);
        }
        if (drawn == 0)
        {
            return null;
        }
        var result = new MemoryStream();
        document.Save(result, closeStream: false);
        result.Position = 0;
        return result;
    }

    private static int FlattenPage(PdfDocument document, PdfPage page)
    {
        if (page.Elements.GetArray("/Annots") is not { } annotations)
        {
            return 0;
        }
        var content = new StringBuilder();
        int count = 0;
        foreach (var item in annotations.Elements)
        {
            if ((item is PdfReference r ? r.Value : item) is not PdfDictionary annotation ||
                Appearance(annotation) is not { } appearance)
            {
                continue;
            }
            string name = AddXObject(document, page, appearance);
            var rect = annotation.Elements.GetRectangle("/Rect");
            var matrix = MatrixOf((PdfDictionary)appearance.Value);
            var box = TransformedBox(((PdfDictionary)appearance.Value).Elements.GetRectangle("/BBox"), matrix);
            if (box.Width <= 0 || box.Height <= 0)
            {
                continue; // an empty appearance shows nothing
            }
            // The matrix that maps the appearance's transformed box onto the annotation's rectangle (the form's own
            // /Matrix is applied by Do).
            double sx = rect.Width / box.Width, sy = rect.Height / box.Height;
            content.Append("q ")
                .Append(Number(sx)).Append(" 0 0 ").Append(Number(sy)).Append(' ')
                .Append(Number(rect.X1 - sx * box.X1)).Append(' ').Append(Number(rect.Y1 - sy * box.Y1))
                .Append(" cm ").Append(name).Append(" Do Q\n");
            count++;
        }
        if (count > 0)
        {
            // The page's own content may leave its graphics state changed: the appearances are drawn after a clean Q.
            page.Contents.PrependContent().CreateStream(Encoding.ASCII.GetBytes("q\n"));
            page.Contents.AppendContent().CreateStream(Encoding.ASCII.GetBytes("Q\n" + content));
        }
        return count;
    }

    /// <summary>The normal appearance a viewer prints (its state by /AS), or null for a hidden or non-printing one.</summary>
    private static PdfReference? Appearance(PdfDictionary annotation)
    {
        int flags = annotation.Elements.GetInteger("/F");
        if ((flags & Print) == 0 || (flags & Hidden) != 0 ||
            annotation.Elements.GetDictionary("/AP") is not { } ap)
        {
            return null;
        }
        var normal = ap.Elements["/N"];
        if (normal is PdfReference { Value: PdfDictionary { Stream: not null } } stream)
        {
            return stream;
        }
        // Appearance states (check boxes, radio buttons): the one /AS names.
        var states = normal is PdfReference sr ? sr.Value as PdfDictionary : normal as PdfDictionary;
        string state = annotation.Elements.GetName("/AS");
        return states != null && state.Length > 0 && states.Elements[state] is PdfReference { Value: PdfDictionary { Stream: not null } } s
            ? s
            : null;
    }

    private static string AddXObject(PdfDocument document, PdfPage page, PdfReference appearance)
    {
        var form = (PdfDictionary)appearance.Value;
        if (!form.Elements.ContainsKey("/Subtype"))
        {
            form.Elements.SetName("/Subtype", "/Form"); // optional in some writers' appearance streams, required for Do
        }
        var resources = page.Resources; // the page's own or inherited ones (PDFsharp copies inherited ones to the page)
        var xobjects = resources.Elements.GetDictionary("/XObject");
        if (xobjects == null)
        {
            xobjects = new PdfDictionary(document);
            resources.Elements["/XObject"] = xobjects;
        }
        int i = 0;
        string name;
        do
        {
            name = $"/XsAnnot{i++}";
        }
        while (xobjects.Elements.ContainsKey(name));
        xobjects.Elements[name] = appearance;
        return name;
    }

    private static double[] MatrixOf(PdfDictionary form)
    {
        if (form.Elements.GetArray("/Matrix") is { Elements.Count: 6 } m)
        {
            return Enumerable.Range(0, 6).Select(m.Elements.GetReal).ToArray();
        }
        return [1, 0, 0, 1, 0, 0];
    }

    /// <summary>The bounding box of the form's box transformed by its matrix (12.5.5, step 1).</summary>
    private static (double X1, double Y1, double Width, double Height) TransformedBox(PdfRectangle bbox, double[] m)
    {
        var xs = new List<double>();
        var ys = new List<double>();
        foreach (var (x, y) in new[] { (bbox.X1, bbox.Y1), (bbox.X2, bbox.Y1), (bbox.X1, bbox.Y2), (bbox.X2, bbox.Y2) })
        {
            xs.Add(m[0] * x + m[2] * y + m[4]);
            ys.Add(m[1] * x + m[3] * y + m[5]);
        }
        return (xs.Min(), ys.Min(), xs.Max() - xs.Min(), ys.Max() - ys.Min());
    }

    private static string Number(double value) => value.ToString("0.#####", CultureInfo.InvariantCulture);
}
