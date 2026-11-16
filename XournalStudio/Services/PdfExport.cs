using System.Xml.Linq;
using PdfSharp.Drawing;
using PdfSharp.Drawing.Layout;
using PdfSharp.Pdf;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>What an export left out: elements the app does not draw yet, pages whose background PDF was not found.</summary>
public sealed record PdfExportResult(int Pages, int SkippedElements, int MissingBackgrounds = 0);

/// <summary>
/// Writes a document as PDF with PDFsharp: each page's background (the lecture PDF's page as vector content, or color
/// and ruling), then its layers in file order – strokes as vector lines (per-segment widths of pressure strokes,
/// highlighters translucent as Xournal stores them), pictures (PNG/JPEG, or the one-page PDF of a copied area) and texts.
/// Works on a detached snapshot (<see cref="XoppDocument.Snapshot"/>), so it can run off the UI thread.
/// </summary>
public static class PdfExport
{
    static PdfExport()
    {
        // Texts use the fonts Windows has (PDFsharp's own resolver; the app's screen uses the same families).
        PdfSharp.Fonts.GlobalFontSettings.UseWindowsFontsUnderWindows = true;
    }

    /// <param name="pictureBackground">The file of page <c>i</c>'s picture background (resolved when loading), or null.</param>
    public static PdfExportResult Write(XDocument snapshot, string? backgroundPdf, string target,
        Func<int, string?>? pictureBackground = null)
    {
        if (ProjectFiles.SamePath(backgroundPdf, target))
        {
            throw new IOException("Die Hintergrund-PDF des Dokuments kann nicht überschrieben werden.");
        }
        var keepOpen = new List<IDisposable>(); // pictures are read when the document is saved
        XPdfForm? background = null;
        try
        {
            using var output = new PdfDocument();
            int pages = 0, skipped = 0, missing = 0;
            var missingFonts = new HashSet<(string, XFontStyleEx)>();
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
                        else if ((string?)bg.Attribute("type") == "pixmap")
                        {
                            if (pictureBackground?.Invoke(pages) is not { } file || !DrawPictureBackground(g, file, width, height, keepOpen))
                            {
                                missing++;
                            }
                        }
                        else if (!XoppPage.IsPdfBackground(bg))
                        {
                            DrawSolid(g, bg, width, height);
                        }
                        else
                        {
                            missing++; // the page stays white under its notes
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
                        else if (XoppText.IsText(element))
                        {
                            DrawText(g, XoppText.Read(element), missingFonts);
                        }
                        else
                        {
                            skipped++; // an element upstream may have that this app does not draw
                        }
                    }
                }
                pages++;
            }
            string temp = target + "~";
            output.Save(temp);
            File.Move(temp, target, overwrite: true);
            return new PdfExportResult(pages, skipped, missing);
        }
        finally
        {
            background?.Dispose();
            keepOpen.ForEach(d => d.Dispose());
        }
    }

    private static void DrawSolid(XGraphics g, XElement background, double width, double height)
    {
        var paper = XoppPage.ColorOf(background);
        g.DrawRectangle(new XSolidBrush(ToX(paper)), 0, 0, width, height);
        foreach (var ruling in BackgroundRuling.For(XoppPage.StyleOf(background), (string?)background.Attribute("config"), paper, width, height))
        {
            var pen = new XPen(ToX(ruling.Color), ruling.Width);
            foreach (var (from, to) in ruling.Lines)
            {
                g.DrawLine(pen, from.X, from.Y, to.X, to.Y);
            }
            var dot = new XSolidBrush(ToX(ruling.Color));
            double r = ruling.Width / 2;
            foreach (var p in ruling.Dots)
            {
                g.DrawEllipse(dot, p.X - r, p.Y - r, 2 * r, 2 * r);
            }
        }
    }

    private static void DrawStroke(XGraphics g, XElement element)
    {
        if (StrokeConverter.ReadShape(element) is not { } shape)
        {
            return;
        }
        // A filled highlighter upstream is one translucent shape with the filling's alpha (a transparency group PDFsharp
        // cannot write): filling and line both get that alpha, the line over the filling comes out a little stronger.
        var color = shape.Highlighter && shape.Fill >= 0
            ? XColor.FromArgb(Math.Clamp(shape.Fill, 0, 255), shape.Color.R, shape.Color.G, shape.Color.B)
            : ToX(shape.Color);
        var cap = shape.Cap switch
        {
            StrokeCap.Butt => XLineCap.Flat,
            StrokeCap.Square => XLineCap.Square,
            _ => XLineCap.Round,
        };
        // Dashes in multiples of the width (as GDI+; which patterns are dashed: StrokeShape.DashesFor); the offset runs
        // the pattern on over the segments of a stroke with pressure as upstream draws it.
        XPen Pen(StrokeLine line)
        {
            var pen = new XPen(color, line.Width) { LineCap = cap, LineJoin = XLineJoin.Round };
            if (shape.DashesFor(line.Width) is { } dashes)
            {
                // PDFsharp rejects zero lengths (the PDF format allows them): a zero one becomes very short – a dash of
                // it is still a dot with round ends.
                pen.DashPattern = dashes.Select(d => Math.Max(d, 1e-3)).ToArray();
                pen.DashOffset = line.DashOffset / line.Width;
            }
            return pen;
        }
        static XPoint[] Points(IReadOnlyList<Windows.Foundation.Point> points) => points.Select(p => new XPoint(p.X, p.Y)).ToArray();
        // What is drawn: StrokeShape.Parts, the same as on screen.
        foreach (var part in shape.Parts())
        {
            switch (part)
            {
                case StrokeFill fill:
                    var filling = XColor.FromArgb(Math.Clamp(shape.Fill, 0, 255), shape.Color.R, shape.Color.G, shape.Color.B);
                    g.DrawPolygon(new XSolidBrush(filling), Points(fill.Points), XFillMode.Winding);
                    break;
                case StrokeDot dot:
                    var brush = new XSolidBrush(color);
                    double r = dot.Size / 2;
                    if (dot.Square)
                    {
                        g.DrawRectangle(brush, dot.Center.X - r, dot.Center.Y - r, dot.Size, dot.Size);
                    }
                    else
                    {
                        g.DrawEllipse(brush, dot.Center.X - r, dot.Center.Y - r, dot.Size, dot.Size);
                    }
                    break;
                case StrokeLine line:
                    g.DrawLines(Pen(line), Points(line.Points)); // one path: a translucent highlighter has no darker joints
                    break;
                default:
                    throw new InvalidOperationException(part.GetType().Name); // as the screen: every part is drawn
            }
        }
    }

    // A picture background PDFsharp cannot read (missing, format, damaged, locked) leaves its page white, as on screen.
    // PDFsharp reads the whole file and decodes it here (FromFile + DrawImage), not when saving.
    private static bool DrawPictureBackground(XGraphics g, string file, double width, double height, List<IDisposable> keepOpen)
    {
        try
        {
            var picture = XImage.FromFile(file);
            keepOpen.Add(picture);
            g.DrawImage(picture, 0, 0, width, height);
            return true;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidOperationException or
            ArgumentException or NotSupportedException or FormatException)
        {
            DebugLog.Write($"export: picture background {file}: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}");
            return false;
        }
    }

    private static void DrawPicture(XGraphics g, XElement element, List<IDisposable> keepOpen)
    {
        byte[] data = XoppImage.GetData(element);
        if (data.Length == 0)
        {
            return;
        }
        var placement = XoppImage.GetPlacement(element);
        var stream = new MemoryStream(data, writable: false);
        keepOpen.Add(stream);
        XImage image = XoppImage.IsPdf(data) ? XPdfForm.FromStream(stream) : XImage.FromStream(stream);
        keepOpen.Add(image);
        var state = g.Save();
        var m = placement.Transform;
        g.MultiplyTransform(new XMatrix(m.M11, m.M12, m.M21, m.M22, m.M31, m.M32));
        g.DrawImage(image, 0, 0, placement.Size.Width, placement.Size.Height);
        g.Restore(state);
    }

    // A family Windows does not have (e.g. a Linux font) falls back to Arial, as the screen's TextBlock falls back;
    // tried once per family and style and export (missing).
    private static XFont Font(string family, double size, XFontStyleEx style, HashSet<(string, XFontStyleEx)> missing)
    {
        var options = new XPdfFontOptions(PdfFontEncoding.Unicode);
        var key = (family.ToLowerInvariant(), style);
        if (!missing.Contains(key))
        {
            try
            {
                return new XFont(family, size, style, options);
            }
            catch (InvalidOperationException e)
            {
                DebugLog.Write($"export: font {family}: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}; Arial instead");
                missing.Add(key);
            }
        }
        return new XFont("Arial", size, style, options);
    }

    // As on screen: the font's family, style and size in points, the box placed by the text's transform, wrapped at its
    // width (if it has one) and aligned.
    private static void DrawText(XGraphics g, XoppText text, HashSet<(string, XFontStyleEx)> missingFonts)
    {
        var style = (text.Bold ? XFontStyleEx.Bold : XFontStyleEx.Regular) | (text.Italic ? XFontStyleEx.Italic : XFontStyleEx.Regular);
        var font = Font(text.WindowsFamily, text.Size, style, missingFonts);
        var brush = new XSolidBrush(ToX(text.Color));
        var state = g.Save();
        var m = text.Transform;
        g.MultiplyTransform(new XMatrix(m.M11, m.M12, m.M21, m.M22, m.M31, m.M32));
        var lines = text.Paragraphs();
        if (text.Wrap is { } wrap)
        {
            // XTextFormatter breaks at spaces only: a word wider than the wrap width runs past the box (the screen's
            // TextBlock breaks it).
            var formatter = new XTextFormatter(g)
            {
                Alignment = text.Justify ? XParagraphAlignment.Justify : text.Alignment switch
                {
                    XoppTextAlignment.Center => XParagraphAlignment.Center,
                    XoppTextAlignment.Right => XParagraphAlignment.Right,
                    _ => XParagraphAlignment.Left,
                },
            };
            formatter.DrawString(string.Join('\n', lines), font, brush, new XRect(0, 0, wrap, 100_000));
        }
        else
        {
            double width = lines.Max(l => g.MeasureString(l, font).Width);
            double lineHeight = font.GetHeight();
            for (int i = 0; i < lines.Length; i++)
            {
                double free = width - g.MeasureString(lines[i], font).Width;
                double x = text.Alignment switch
                {
                    XoppTextAlignment.Center => free / 2,
                    XoppTextAlignment.Right => free,
                    _ => 0,
                };
                g.DrawString(lines[i], font, brush, x, i * lineHeight, XStringFormats.TopLeft);
            }
        }
        g.Restore(state);
    }

    private static XColor ToX(Windows.UI.Color c) => XColor.FromArgb(c.A, c.R, c.G, c.B);
}
