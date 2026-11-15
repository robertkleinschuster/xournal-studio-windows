using System.Xml.Linq;
using PdfSharp.Drawing;
using PdfSharp.Pdf;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>What an export left out (the app does not draw these yet).</summary>
public sealed record PdfExportResult(int Pages, int SkippedElements);

/// <summary>
/// Writes a document as PDF with PDFsharp: each page's background (the lecture PDF's page as vector content, or color
/// and ruling), then its layers in file order – strokes as vector lines (per-segment widths of pressure strokes,
/// highlighters translucent as Xournal stores them) and pictures (PNG/JPEG, or the one-page PDF of a copied area).
/// Works on a detached snapshot (<see cref="XoppDocument.Snapshot"/>), so it can run off the UI thread.
/// </summary>
public static class PdfExport
{
    public static PdfExportResult Write(XDocument snapshot, string? backgroundPdf, string target)
    {
        if (backgroundPdf != null && string.Equals(Path.GetFullPath(backgroundPdf), Path.GetFullPath(target),
                StringComparison.OrdinalIgnoreCase))
        {
            throw new IOException("Die Hintergrund-PDF des Dokuments kann nicht überschrieben werden.");
        }
        var keepOpen = new List<IDisposable>(); // pictures are read when the document is saved
        XPdfForm? background = null;
        try
        {
            using var output = new PdfDocument();
            int pages = 0, skipped = 0;
            foreach (var page in snapshot.Root!.Elements("page"))
            {
                double width = (double?)page.Attribute("width") ?? XoppDocument.A4Width;
                double height = (double?)page.Attribute("height") ?? XoppDocument.A4Height;
                var pdfPage = output.AddPage();
                pdfPage.Width = XUnit.FromPoint(width);
                pdfPage.Height = XUnit.FromPoint(height);
                using (var g = XGraphics.FromPdfPage(pdfPage))
                {
                    if (page.Element("background") is { } bg)
                    {
                        if (XoppPage.IsPdfBackground(bg) && backgroundPdf != null && File.Exists(backgroundPdf))
                        {
                            background ??= XPdfForm.FromFile(backgroundPdf);
                            background.PageNumber = Math.Min(XoppPage.PdfPageIndexOf(bg) + 1, background.PageCount);
                            g.DrawImage(background, 0, 0, width, height);
                        }
                        else if (!XoppPage.IsPdfBackground(bg))
                        {
                            DrawSolid(g, bg, width, height);
                        }
                    }
                    foreach (var element in page.Elements("layer").Elements())
                    {
                        if (element.Name == "stroke")
                        {
                            DrawStroke(g, element);
                        }
                        else if (XoppImage.IsImage(element))
                        {
                            DrawPicture(g, element, keepOpen);
                        }
                        else
                        {
                            skipped++; // e.g. <text>
                        }
                    }
                }
                pages++;
            }
            string temp = target + "~";
            output.Save(temp);
            File.Move(temp, target, overwrite: true);
            return new PdfExportResult(pages, skipped);
        }
        finally
        {
            background?.Dispose();
            keepOpen.ForEach(d => d.Dispose());
        }
    }

    private static void DrawSolid(XGraphics g, XElement background, double width, double height)
    {
        g.DrawRectangle(new XSolidBrush(ToX(XoppPage.ColorOf(background))), 0, 0, width, height);
        if (BackgroundRuling.For(XoppPage.StyleOf(background), width, height) is not { } ruling)
        {
            return;
        }
        var pen = new XPen(ToX(ruling.Color), Ruling.LineWidth);
        foreach (var (from, to) in ruling.Lines)
        {
            g.DrawLine(pen, from.X, from.Y, to.X, to.Y);
        }
        var dot = new XSolidBrush(ToX(ruling.Color));
        foreach (var p in ruling.Dots)
        {
            g.DrawEllipse(dot, p.X - Ruling.DotRadius, p.Y - Ruling.DotRadius, 2 * Ruling.DotRadius, 2 * Ruling.DotRadius);
        }
    }

    private static void DrawStroke(XGraphics g, XElement element)
    {
        if (StrokeConverter.ReadShape(element) is not { } shape)
        {
            return;
        }
        var color = ToX(shape.Color);
        XPen Pen(double width) => new(color, width) { LineCap = XLineCap.Round, LineJoin = XLineJoin.Round };
        var points = shape.Points.Select(p => new XPoint(p.X, p.Y)).ToArray();
        if (points.Length == 1)
        {
            g.DrawLine(Pen(shape.Width), points[0], points[0]); // a dot (round cap)
        }
        else if (shape.EvenWidth)
        {
            g.DrawLines(Pen(shape.Width), points); // one path: a translucent highlighter has no darker joints
        }
        else
        {
            for (int i = 0; i < points.Length - 1; i++)
            {
                g.DrawLine(Pen(shape.SegmentWidths[i]), points[i], points[i + 1]);
            }
        }
    }

    private static void DrawPicture(XGraphics g, XElement element, List<IDisposable> keepOpen)
    {
        byte[] data = XoppImage.GetData(element);
        if (data.Length == 0)
        {
            return;
        }
        var bounds = XoppImage.GetBounds(element);
        var stream = new MemoryStream(data, writable: false);
        keepOpen.Add(stream);
        XImage image = XoppImage.IsPdf(data) ? XPdfForm.FromStream(stream) : XImage.FromStream(stream);
        keepOpen.Add(image);
        g.DrawImage(image, bounds.X, bounds.Y, bounds.Width, bounds.Height);
    }

    private static XColor ToX(Windows.UI.Color c) => XColor.FromArgb(c.A, c.R, c.G, c.B);
}
