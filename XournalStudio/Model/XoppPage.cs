using System.Xml.Linq;
using Microsoft.UI.Xaml.Controls;
using System.Numerics;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using Windows.UI;

namespace XournalStudio.Model;

public enum BackgroundStyle { Plain, Lined, Ruled, Graph, Dotted }

/// <summary>
/// One page. Its strokes live in an <see cref="InkStrokeContainer"/> (shown by the page's InkCanvas);
/// pictures and all other content stay in the page's XML element and are written back as loaded.
/// </summary>
public sealed class XoppPage
{
    private readonly XElement element;

    // Stroke elements loaded from (or already written to) the file, by ink stroke id. Unchanged
    // strokes are written back as they were, so attributes this app ignores survive a save.
    private readonly Dictionary<uint, XElement> strokeElements = new();

    // Undo/redo cannot re-add a removed stroke and adds a copy: old id -> id of the copy.
    private readonly Dictionary<uint, uint> replacedIds = new();

    public double Width { get; }
    public double Height { get; }

    public XElement Background => element.Element("background")!;

    // Strokes read from the file until the page's InkCanvas takes them over.
    private List<(InkStroke Stroke, XElement Element)>? pendingStrokes = new();

    /// <summary>The stroke container of the page's InkCanvas (the canvas owns it), once attached.</summary>
    public InkStrokeContainer? Ink { get; private set; }

    public IReadOnlyList<InkStroke> Strokes =>
        Ink?.GetStrokes() ?? (IReadOnlyList<InkStroke>?)pendingStrokes?.Select(p => p.Stroke).ToList() ?? [];

    public bool IsPdf => (string?)Background.Attribute("type") == "pdf";

    /// <summary>0-based page index in the background PDF.</summary>
    public int PdfPageIndex
    {
        get
        {
            // Older files contain values like "3ll".
            var digits = new string(((string?)Background.Attribute("pageno") ?? "1").TakeWhile(char.IsDigit).ToArray());
            return Math.Max(0, (int.TryParse(digits, out int n) ? n : 1) - 1);
        }
    }

    public Color BackgroundColor => XoppColor.Parse((string?)Background.Attribute("color"), Microsoft.UI.Colors.White);

    public BackgroundStyle Style => (string?)Background.Attribute("style") switch
    {
        "lined" => BackgroundStyle.Lined,
        "ruled" => BackgroundStyle.Ruled,
        "graph" => BackgroundStyle.Graph,
        "dotted" => BackgroundStyle.Dotted,
        _ => BackgroundStyle.Plain,
    };

    private XoppPage(XElement element)
    {
        this.element = element;
        Width = (double?)element.Attribute("width") ?? XoppDocument.A4Width;
        Height = (double?)element.Attribute("height") ?? XoppDocument.A4Height;
        if (element.Element("background") == null)
        {
            element.AddFirst(SolidBackground("plain"));
        }
        if (!element.Elements("layer").Any())
        {
            element.Add(new XElement("layer"));
        }
    }

    public static XoppPage CreateGraph(double width, double height) =>
        new(new XElement("page", new XAttribute("width", width), new XAttribute("height", height),
            SolidBackground("graph"), new XElement("layer")));

    public static XoppPage CreatePdf(double width, double height, int pdfPageIndex) =>
        new(new XElement("page", new XAttribute("width", width), new XAttribute("height", height),
            new XElement("background", new XAttribute("type", "pdf"), new XAttribute("pageno", pdfPageIndex + 1)),
            new XElement("layer")));

    public static XoppPage FromXml(XElement element)
    {
        var page = new XoppPage(element);
        foreach (var strokeElement in element.Elements("layer").Elements("stroke"))
        {
            var stroke = StrokeConverter.FromXml(strokeElement);
            if (stroke != null)
            {
                page.pendingStrokes!.Add((stroke, strokeElement));
            }
        }
        return page;
    }

    /// <summary>Hands the page's strokes to its InkCanvas. Only once per page.</summary>
    public void AttachInk(InkStrokeContainer container)
    {
        if (Ink != null)
        {
            throw new InvalidOperationException("The page is already shown.");
        }
        Ink = container;
        foreach (var (stroke, e) in pendingStrokes!)
        {
            container.AddStroke(stroke);
            strokeElements[stroke.Id] = e;
        }
        pendingStrokes = null;
    }

    /// <summary>Called when undo/redo puts a copy of a stroke back in place of the removed original.</summary>
    public void TransferStrokeIdentity(uint oldId, uint newId)
    {
        replacedIds[oldId] = newId;
        if (strokeElements.Remove(oldId, out var e))
        {
            strokeElements[newId] = e;
        }
    }

    /// <summary>
    /// The stroke on the page now for a stroke an edit remembers: itself, or the copy undo/redo put in its place;
    /// null if it is not on the page.
    /// </summary>
    public InkStroke? Current(InkStroke stroke)
    {
        uint id = stroke.Id;
        while (replacedIds.TryGetValue(id, out uint next))
        {
            id = next;
        }
        return Ink?.GetStrokes().FirstOrDefault(s => s.Id == id);
    }

    /// <summary>
    /// Moves/scales strokes: the ink by their <c>PointTransform</c> (Windows ink redraws them), their original
    /// elements by the same transform; strokes drawn in this session are written from the ink anyway.
    /// </summary>
    public void TransformStrokes(IEnumerable<InkStroke> strokes, Matrix3x2 m)
    {
        foreach (var stroke in strokes)
        {
            stroke.PointTransform *= m;
            RecordTransform(stroke, m);
        }
    }

    /// <summary>The ink already shows the stroke transformed (live while dragging): only its element follows.</summary>
    public void RecordTransform(InkStroke stroke, Matrix3x2 m)
    {
        if (strokeElements.TryGetValue(stroke.Id, out var e))
        {
            StrokeConverter.Transform(e, m);
        }
    }

    /// <summary>Moves/scales a picture (in place: its layer and position among the elements stay).</summary>
    public void TransformImage(XElement image, Matrix3x2 m)
    {
        XoppImage.SetBounds(image, XoppImage.Transform(XoppImage.GetBounds(image), m));
        ImagesChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>The stroke as a .xopp element: its original element (exact widths, unknown attributes) or converted.</summary>
    public XElement ElementOf(InkStroke stroke) =>
        strokeElements.TryGetValue(stroke.Id, out var e) ? new XElement(e) : StrokeConverter.ToXml(stroke);

    /// <summary>
    /// Adds a stroke made from a .xopp element (pasted, duplicated): the element is written as it is, so a copy keeps
    /// exactly what the original had. Null if the element has no points.
    /// </summary>
    public InkStroke? AddStrokeElement(XElement e)
    {
        var stroke = StrokeConverter.FromXml(e);
        if (stroke == null)
        {
            return null;
        }
        Ink!.AddStroke(stroke);
        strokeElements[stroke.Id] = e;
        element.Elements("layer").Last().Add(e);
        return stroke;
    }

    /// <summary>Pictures (&lt;image&gt;, &lt;teximage&gt;) of all layers, bottom to top.</summary>
    public IEnumerable<XElement> Images => element.Elements("layer").Elements().Where(XoppImage.IsImage);

    /// <summary>Raised when pictures are added or removed.</summary>
    public event EventHandler? ImagesChanged;

    /// <summary>Adds a picture (new ones go to the top layer, like strokes).</summary>
    public void AddImage(XElement image)
    {
        element.Elements("layer").Last().Add(image);
        ImagesChanged?.Invoke(this, EventArgs.Empty);
    }

    public void RemoveImage(XElement image)
    {
        if (image.Parent != null)
        {
            image.Remove();
            ImagesChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>The lowest edge of the page's content (strokes and pictures), 0 if empty.</summary>
    public double ContentBottom()
    {
        double bottom = 0;
        foreach (var s in Strokes)
        {
            bottom = Math.Max(bottom, s.BoundingRect.Bottom);
        }
        foreach (var image in Images)
        {
            bottom = Math.Max(bottom, XoppImage.GetBounds(image).Bottom);
        }
        return bottom;
    }

    /// <summary>Synchronizes the XML element with the ink and returns it.</summary>
    public XElement ToXml()
    {
        if (Ink == null)
        {
            return element; // never shown: unchanged
        }
        var strokes = Ink.GetStrokes();
        var liveIds = strokes.Select(s => s.Id).ToHashSet();

        foreach (var (id, e) in strokeElements.ToList())
        {
            if (!liveIds.Contains(id))
            {
                e.Remove();
                strokeElements.Remove(id);
            }
        }

        // New strokes go to the top layer, where Xournal draws by default as well.
        var layer = element.Elements("layer").Last();
        foreach (var stroke in strokes)
        {
            if (!strokeElements.ContainsKey(stroke.Id))
            {
                var e = StrokeConverter.ToXml(stroke);
                layer.Add(e);
                strokeElements[stroke.Id] = e;
            }
        }
        return element;
    }

    private static XElement SolidBackground(string style) =>
        new("background", new XAttribute("type", "solid"), new XAttribute("color", "#ffffffff"),
            new XAttribute("style", style));
}
