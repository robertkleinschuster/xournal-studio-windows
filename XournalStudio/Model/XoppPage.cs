using System.Xml.Linq;
using Microsoft.UI.Xaml.Controls;
using System.Numerics;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using Windows.UI;

namespace XournalStudio.Model;

/// <summary>The "style" of a solid background (upstream PageTypeFormat; "lined" = ruled with a margin line).</summary>
public enum BackgroundStyle { Plain, Lined, Ruled, Graph, Dotted, Staves, IsoDotted, IsoGraph }

/// <summary>Where a removed picture was: its layer and its position among the layer's elements.</summary>
public sealed record ImagePlace(System.Xml.Linq.XElement Image, System.Xml.Linq.XElement Layer, int Index);

/// <summary>Where strokes and pictures were (see <see cref="XoppPage.CapturePlacement"/>).</summary>
public sealed record Placement(
    List<(InkStroke Stroke, System.Numerics.Matrix3x2 Transform, string? Coordinates)> Strokes,
    List<(System.Xml.Linq.XElement Image, Windows.Foundation.Rect Bounds)> Images);

/// <summary>
/// One page. While it has a view its strokes live in the view's <see cref="InkStrokeContainer"/> (shown by its
/// InkCanvas); without a view the page is only its XML element (strokes read for a view, written back when it goes).
/// Pictures and all other content stay in the page's XML element and are written back as loaded.
/// </summary>
public sealed class XoppPage
{
    private readonly XElement element;

    // Stroke elements loaded from (or already written to) the file, by ink stroke id. Unchanged
    // strokes are written back as they were, so attributes this app ignores survive a save.
    private readonly Dictionary<uint, XElement> strokeElements = new();

    // Strokes drawn in this session, as written (see Snapshot).
    private readonly ConvertedStrokes converted = new();

    /// <summary>How many drawn strokes snapshots converted so far (for tests: each once per transform).</summary>
    internal int StrokeConversions => converted.Conversions;

    // Undo/redo cannot re-add a removed stroke and adds a copy: old id -> id of the copy.

    public double Width { get; }
    public double Height { get; }

    public XElement Background => element.Element("background")!;

    /// <summary>The stroke container of the page's InkCanvas (the canvas owns it), while the page has a view.</summary>
    public InkStrokeContainer? Ink { get; private set; }

    /// <summary>The page's ink strokes: those of its view, or (no view) read from its element.</summary>
    public IReadOnlyList<InkStroke> Strokes => Ink?.GetStrokes() ?? ReadStrokes().Select(s => s.Stroke).ToList();

    public bool IsPdf => IsPdfBackground(Background);

    /// <summary>0-based page index in the background PDF.</summary>
    public int PdfPageIndex => PdfPageIndexOf(Background);

    public Color BackgroundColor => ColorOf(Background);

    public BackgroundStyle Style => StyleOf(Background);

    /// <summary>The background's "config" attribute (ruling parameters, see <see cref="BackgroundRuling"/>).</summary>
    public string? BackgroundConfig => (string?)Background.Attribute("config");

    /// <summary>A picture as background (type "pixmap").</summary>
    public bool IsImageBackground => (string?)Background.Attribute("type") == "pixmap";

    /// <summary>The file a picture background names, resolved when the document was loaded (it may be missing; null: none).</summary>
    public string? BackgroundImagePath { get; set; }

    /// <summary>The page whose picture background this page's "clone" background shows (null: not a clone).</summary>
    public XoppPage? BackgroundCloneSource { get; set; }

    /// <summary>0-based page index in the background PDF of a &lt;background&gt; element.</summary>
    public static int PdfPageIndexOf(XElement background)
    {
        // Older files contain values like "3ll".
        var digits = new string(((string?)background.Attribute("pageno") ?? "1").TakeWhile(char.IsDigit).ToArray());
        return Math.Max(0, (int.TryParse(digits, out int n) ? n : 1) - 1);
    }

    public static bool IsPdfBackground(XElement background) => (string?)background.Attribute("type") == "pdf";

    public static Color ColorOf(XElement background) =>
        XoppColor.Parse((string?)background.Attribute("color"), Microsoft.UI.Colors.White);

    public static BackgroundStyle StyleOf(XElement background) => (string?)background.Attribute("style") switch
    {
        "lined" => BackgroundStyle.Lined,
        "ruled" => BackgroundStyle.Ruled,
        "graph" => BackgroundStyle.Graph,
        "dotted" => BackgroundStyle.Dotted,
        "staves" => BackgroundStyle.Staves,
        "isodotted" => BackgroundStyle.IsoDotted,
        "isograph" => BackgroundStyle.IsoGraph,
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

    public static XoppPage FromXml(XElement element) => new(element);

    /// <summary>
    /// The page's ink strokes made from its stroke elements, in drawing order, each with its element (strokes drawn from
    /// the file, <see cref="StrokeConverter.NeedsOwnDrawing"/>, and strokes without points are not ink). Runs on any
    /// thread while the page has no view: then nothing changes the page's element (it is not in the document's tree, and
    /// only a view edits a page), so a view is prepared off the UI thread.
    /// </summary>
    public List<(InkStroke Stroke, XElement Element)> ReadStrokes()
    {
        var strokes = new List<(InkStroke, XElement)>();
        foreach (var strokeElement in element.Elements("layer").Elements("stroke"))
        {
            if (StrokeConverter.InkOf(strokeElement) is { } stroke)
            {
                strokes.Add((stroke, strokeElement));
            }
        }
        return strokes;
    }

    /// <summary>
    /// Hands the page's strokes (<see cref="ReadStrokes"/>, read since the page had its last view) to the InkCanvas of a
    /// new view; from now on the ink is the page's strokes.
    /// </summary>
    public void AttachInk(InkStrokeContainer container, IEnumerable<(InkStroke Stroke, XElement Element)> strokes)
    {
        if (Ink != null)
        {
            throw new InvalidOperationException("The page is already shown.");
        }
        var list = strokes.ToList();
        foreach (var (stroke, _) in list)
        {
            container.AddStroke(stroke);
        }
        Adopt(list);
        Ink = container;
    }

    // The strokes' elements, by ink stroke id (the ink of a new view).
    internal void Adopt(IEnumerable<(InkStroke Stroke, XElement Element)> strokes)
    {
        foreach (var (stroke, e) in strokes)
        {
            strokeElements[stroke.Id] = e;
        }
    }

    /// <summary>
    /// The page's view is released (far from the viewport): the ink is written into the page's element as a snapshot
    /// would write it (<see cref="WriteBack"/>), and the page keeps no ink objects – the next view reads its strokes anew.
    /// Only for a page no edit in the history refers to (the edits hold the view's strokes).
    /// </summary>
    public void DetachInk()
    {
        if (Ink == null)
        {
            throw new InvalidOperationException("The page is not shown.");
        }
        WriteBack(Ink.GetStrokes());
        Ink = null;
    }

    /// <summary>
    /// Writes these strokes (the ink, in drawing order) into the page's element, exactly as <see cref="SnapshotOf"/>
    /// writes them: elements of strokes no longer on the ink are taken out, strokes added in this session (drawn:
    /// converted; pasted/duplicated: their element) follow in the top layer; everything else stays as it is. The page then
    /// knows no stroke by id any more.
    /// </summary>
    internal void WriteBack(IReadOnlyList<InkStroke> strokes)
    {
        var live = strokes.Select(s => s.Id).ToHashSet();
        foreach (var (id, e) in strokeElements)
        {
            if (!live.Contains(id) && e.Parent != null)
            {
                e.Remove();
            }
        }
        var topLayer = element.Elements("layer").Last();
        foreach (var stroke in strokes)
        {
            if (!strokeElements.TryGetValue(stroke.Id, out var e))
            {
                topLayer.Add(converted.ElementOf(stroke, CommittedTransform(stroke) ?? stroke.PointTransform));
            }
            else if (e.Parent == null)
            {
                topLayer.Add(e);
            }
        }
        strokeElements.Clear();
        previewed.Clear();
        converted.KeepOnly(new HashSet<uint>());
    }

    /// <summary>
    /// Undo/redo put a copy of a stroke back in place of the removed original (the library follows the chain of
    /// identities: <c>InkPage.StrokeReplaced</c>): the stroke's file element and its converted element go to its copy.
    /// </summary>
    public void RenameStroke(uint oldId, uint newId)
    {
        if (strokeElements.Remove(oldId, out var e))
        {
            strokeElements[newId] = e;
        }
        converted.Transfer(oldId, newId);
    }

    /// <summary>
    /// Where strokes and pictures are: each stroke's <c>PointTransform</c> and its element's coordinates (if it has
    /// one), each picture's bounds – to put them back exactly (undo/redo of a move or scale, no rounding).
    /// </summary>
    public Placement CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<XElement> images,
        Func<InkStroke, Matrix3x2>? transformOf = null) => new(
        strokes.Select(s => (s, transformOf?.Invoke(s) ?? s.PointTransform,
            strokeElements.TryGetValue(s.Id, out var e) ? e.Value : null)).ToList(),
        images.Select(i => (i, XoppImage.GetBounds(i))).ToList());

    /// <param name="current">The placement's strokes as they are on the page now (the copies undo/redo put in their place).</param>
    public void RestorePlacement(Placement placement, IReadOnlyList<InkStroke?> current)
    {
        for (int i = 0; i < current.Count; i++)
        {
            var (_, transform, coordinates) = placement.Strokes[i];
            if (current[i] is not { } stroke)
            {
                continue;
            }
            stroke.PointTransform = transform;
            if (coordinates != null && strokeElements.TryGetValue(stroke.Id, out var e))
            {
                e.Value = coordinates;
            }
        }
        foreach (var (image, bounds) in placement.Images)
        {
            XoppImage.SetBounds(image, bounds);
        }
        if (placement.Images.Count > 0)
        {
            ImagesMoved?.Invoke(this, placement.Images.Select(i => i.Image).ToList());
        }
    }

    /// <summary>
    /// A move/scale is previewed on this stroke (its PointTransform shows it): until it is committed or reverted the
    /// page writes the stroke with <paramref name="committed"/>, so a snapshot (saving, recovery copy, export) never holds
    /// a half-done move – whenever it is taken.
    /// </summary>
    public void BeginPreview(InkStroke stroke, Matrix3x2 committed) => previewed[stroke.Id] = committed;

    /// <summary>The transform written for a stroke while a move is previewed on it (see <see cref="BeginPreview"/>), or null.</summary>
    public Matrix3x2? CommittedTransform(InkStroke stroke) => previewed.TryGetValue(stroke.Id, out var t) ? t : null;

    /// <summary>The preview of these strokes ended (committed or reverted): their PointTransform is written again.</summary>
    public void EndPreview(IEnumerable<InkStroke> strokes)
    {
        foreach (var stroke in strokes)
        {
            previewed.Remove(stroke.Id);
        }
    }

    // Stroke id → its committed transform while a move is previewed on it.
    private readonly Dictionary<uint, Matrix3x2> previewed = new();

    /// <summary>The ink already shows the stroke transformed (live while dragging): only its element follows.</summary>
    public void RecordTransform(InkStroke stroke, Matrix3x2 m)
    {
        if (strokeElements.TryGetValue(stroke.Id, out var e))
        {
            StrokeConverter.Transform(e, m);
        }
    }

    /// <summary>Moves/scales pictures (in place: their layer and position among the elements stay).</summary>
    public void TransformImages(IReadOnlyCollection<XElement> images, Matrix3x2 m)
    {
        foreach (var image in images)
        {
            XoppImage.SetBounds(image, XoppImage.Transform(XoppImage.GetBounds(image), m));
        }
        ImagesMoved?.Invoke(this, images);
    }

    /// <summary>The stroke as a .xopp element: its original element (exact widths, unknown attributes) or converted.</summary>
    public XElement ElementOf(InkStroke stroke) =>
        strokeElements.TryGetValue(stroke.Id, out var e) ? new XElement(e) : StrokeConverter.ToXml(stroke);

    /// <summary>
    /// Adds a stroke made from a .xopp element (pasted, duplicated): the element is written as it is, so a copy keeps
    /// exactly what the original had. Null when it cannot be ink: no points, or a stroke this app draws from the file
    /// (styled or damaged, <see cref="StrokeConverter.NeedsOwnDrawing"/>) – as ink it would look other than it means.
    /// </summary>
    public InkStroke? AddStrokeElement(XElement e)
    {
        var stroke = StrokeConverter.InkOf(e);
        if (stroke == null)
        {
            return null;
        }
        Ink!.AddStroke(stroke);
        strokeElements[stroke.Id] = e; // written with the new strokes, in drawing order (see Snapshot)
        return stroke;
    }

    /// <summary>Strokes shown from the file, not edited (<see cref="StrokeConverter.NeedsOwnDrawing"/>).</summary>
    public IReadOnlyList<XElement> DrawnStrokes => drawnStrokes ??=
        element.Elements("layer").Elements("stroke").Where(StrokeConverter.NeedsOwnDrawing).ToList();

    // Found once (the page view draws them once too): nothing edits these elements – they are not ink – and added
    // strokes are ink (AddStrokeElement adds no other), kept beside the page's tree, never in it.
    private List<XElement>? drawnStrokes;

    /// <summary>Texts and links of all layers, bottom to top (shown, not edited).</summary>
    public IEnumerable<XElement> Texts => element.Elements("layer").Elements().Where(XoppText.IsText);

    /// <summary>Pictures (&lt;image&gt;, &lt;teximage&gt;) of all layers, bottom to top.</summary>
    public IEnumerable<XElement> Images => element.Elements("layer").Elements().Where(XoppImage.IsImage);

    /// <summary>Raised when pictures are added or removed (put back by undo): those pictures.</summary>
    public event EventHandler<IReadOnlyCollection<XElement>>? ImagesChanged;

    /// <summary>Raised when pictures got new bounds (moved, scaled).</summary>
    public event EventHandler<IReadOnlyCollection<XElement>>? ImagesMoved;

    /// <summary>Adds elements to the top layer of a page without view (building a document: the iPad import).</summary>
    public void AddToTopLayer(IEnumerable<XElement> elements) => element.Elements("layer").Last().Add(elements);

    /// <summary>Adds pictures (new ones go to the top layer, like strokes).</summary>
    public void AddImages(IReadOnlyCollection<XElement> images)
    {
        element.Elements("layer").Last().Add(images);
        RaiseImagesChanged(images);
    }

    /// <summary>Removes pictures; returns where they were (layer and position) for <see cref="RestoreImages"/>.</summary>
    public List<ImagePlace> RemoveImages(IReadOnlyCollection<XElement> images)
    {
        var places = images.Where(i => i.Parent != null)
            .Select(i => new ImagePlace(i, i.Parent!, i.ElementsBeforeSelf().Count())).ToList();
        foreach (var place in places)
        {
            place.Image.Remove();
        }
        RaiseImagesChanged(images);
        return places;
    }

    /// <summary>Puts removed pictures back at their place (undo: the file keeps its layers and order).</summary>
    public void RestoreImages(IReadOnlyCollection<ImagePlace> places)
    {
        foreach (var place in places.OrderBy(p => p.Index))
        {
            var next = place.Layer.Elements().ElementAtOrDefault(place.Index);
            if (next != null)
            {
                next.AddBeforeSelf(place.Image);
            }
            else
            {
                place.Layer.Add(place.Image);
            }
        }
        RaiseImagesChanged(places.Select(p => p.Image).ToList());
    }

    // Once per change, with the pictures changed: the page view shows or drops exactly those.
    private void RaiseImagesChanged(IReadOnlyCollection<XElement> images)
    {
        if (images.Count > 0)
        {
            ImagesChanged?.Invoke(this, images);
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
        foreach (var shape in DrawnStrokes.Select(StrokeConverter.ReadShape).OfType<StrokeShape>())
        {
            if (shape.Bottom() is { } lowest) // only what is drawn
            {
                bottom = Math.Max(bottom, lowest);
            }
        }
        foreach (var text in Texts.Select(XoppText.Read))
        {
            // The laid-out box is the view's; estimated here (a line per paragraph at 1.2 × the size, about 0.6 × the
            // size per character, or the wrap width) – close enough to place pasted content below it. All four corners:
            // a matrix may turn or mirror the box.
            // A wrapped paragraph takes about (its estimated width / the wrap width) lines, at most one per character.
            double charWidth = text.Size * 0.6;
            // Characters as shown (text elements: an emoji or a letter with its accents is one).
            var lengths = text.Paragraphs().Select(l => new System.Globalization.StringInfo(l).LengthInTextElements).ToList();
            float w = (float)(text.Wrap ?? lengths.Max() * charWidth);
            double lines = text.Wrap is { } wrap
                ? lengths.Sum(n => Math.Clamp(Math.Ceiling(n * charWidth / wrap), 1, Math.Max(1, n)))
                : lengths.Count;
            float h = (float)(lines * text.Size * 1.2);
            foreach (var corner in new[] { Vector2.Zero, new Vector2(w, 0), new Vector2(0, h), new Vector2(w, h) })
            {
                bottom = Math.Max(bottom, Vector2.Transform(corner, text.Transform).Y);
            }
        }
        return bottom;
    }

    /// <summary>
    /// The page as it is now, as a detached copy (reading only: the page keeps every element it has, so an undo can
    /// bring a deleted stroke back with its original element). Strokes on the ink are their original elements; strokes
    /// added in this session (drawn: converted; pasted/duplicated: their element) follow in the top layer in drawing
    /// order, where Xournal draws by default as well; elements of
    /// strokes no longer on the ink are left out; everything else (unknown elements too) is copied as it is.
    /// </summary>
    public XElement Snapshot()
    {
        // Without view the element is the page as it is (a released view wrote its ink into it).
        return Ink != null ? SnapshotOf(Ink.GetStrokes()) : new XElement(element);
    }

    /// <summary>The page as <see cref="Snapshot"/> writes it with these strokes on its ink (in drawing order).</summary>
    internal XElement SnapshotOf(IReadOnlyList<InkStroke> strokes)
    {
        var live = strokes.Select(s => s.Id).ToHashSet();
        var idOf = strokeElements.ToDictionary(p => p.Value, p => p.Key);
        var copy = new XElement(element.Name, element.Attributes());
        XElement? topLayer = null;
        foreach (var node in element.Nodes())
        {
            if (node is XElement { Name.LocalName: "layer" } layer)
            {
                var layerCopy = new XElement(layer.Name, layer.Attributes());
                foreach (var child in layer.Nodes())
                {
                    bool removed = child is XElement e && idOf.TryGetValue(e, out uint id) && !live.Contains(id);
                    if (!removed)
                    {
                        layerCopy.Add(child is XElement el ? new XElement(el) : child);
                    }
                }
                copy.Add(layerCopy);
                topLayer = layerCopy;
            }
            else
            {
                copy.Add(node is XElement el ? new XElement(el) : node);
            }
        }
        foreach (var stroke in strokes)
        {
            if (!strokeElements.TryGetValue(stroke.Id, out var e))
            {
                // Drawn in this session: converted once per transform, not on every recovery copy.
                topLayer!.Add(converted.ElementOf(stroke, CommittedTransform(stroke) ?? stroke.PointTransform));
            }
            else if (e.Parent == null)
            {
                topLayer!.Add(new XElement(e)); // pasted or duplicated: its element, at its place in the drawing order
            }
        }
        converted.KeepOnly(live);
        return copy;
    }

    private static XElement SolidBackground(string style) =>
        new("background", new XAttribute("type", "solid"), new XAttribute("color", "#ffffffff"),
            new XAttribute("style", style));
}
