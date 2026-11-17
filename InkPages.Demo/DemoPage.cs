using System.Numerics;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.UI;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace InkPages.Demo;

/// <summary>A picture of the demo: a colored rectangle (stands for an app's pictures, e.g. pasted images).</summary>
public sealed class DemoPicture(Rect bounds, Color color)
{
    public Rect Bounds { get; set; } = bounds;
    public Color Color { get; } = color;
}

/// <summary>
/// A page of the demo: a graph-paper background, pictures as colored rectangles, the ink. The content lives in memory
/// only – what an app keeps in its file (stroke data, pictures) is the smallest that still exercises every member of
/// <see cref="InkPage"/>.
/// </summary>
public sealed partial class DemoPage : InkPage
{
    private const double Grid = 14.17; // 5 mm

    private readonly XamlCanvas pictureLayer = new();
    private readonly List<DemoPicture> pictures = new();
    private readonly Dictionary<DemoPicture, Border> shown = new();
    private readonly Dictionary<uint, uint> replacedIds = new();

    public DemoPage(double width, double height) : base(width, height)
    {
        SheetBackground = new SolidColorBrush(Colors.White);
        UnderInk.Children.Add(GraphPaper(width, height));
        UnderInk.Children.Add(pictureLayer);
    }

    private static Microsoft.UI.Xaml.Shapes.Path GraphPaper(double width, double height)
    {
        var lines = new GeometryGroup();
        for (double x = Grid; x < width; x += Grid)
        {
            lines.Children.Add(new LineGeometry { StartPoint = new Point(x, 0), EndPoint = new Point(x, height) });
        }
        for (double y = Grid; y < height; y += Grid)
        {
            lines.Children.Add(new LineGeometry { StartPoint = new Point(0, y), EndPoint = new Point(width, y) });
        }
        return new Microsoft.UI.Xaml.Shapes.Path
        {
            Data = lines,
            Stroke = new SolidColorBrush(Color.FromArgb(255, 0xbd, 0xbd, 0xbd)),
            StrokeThickness = 0.5,
        };
    }

    // ---------------------------------------------------------------- strokes

    public override void StrokeReplaced(uint oldId, uint newId)
    {
        while (replacedIds.TryGetValue(oldId, out uint next))
        {
            oldId = next;
        }
        replacedIds[oldId] = newId;
    }

    public override List<InkStroke?> CurrentStrokes(IEnumerable<InkStroke> strokes)
    {
        var byId = Ink.StrokeContainer.GetStrokes().ToDictionary(s => s.Id);
        return strokes.Select(stroke =>
        {
            uint id = stroke.Id;
            while (replacedIds.TryGetValue(id, out uint next))
            {
                id = next;
            }
            return byId.GetValueOrDefault(id);
        }).ToList();
    }

    // The demo keeps no data of its own per stroke: the ink is all there is.
    public override void BeginStrokePreview(InkStroke stroke, Matrix3x2 committed) { }

    public override void EndStrokePreview(IEnumerable<InkStroke> strokes) { }

    public override void StrokeTransformed(InkStroke stroke, Matrix3x2 m) { }

    private sealed record Placement(List<(InkStroke Stroke, Matrix3x2 Transform)> Strokes, List<(DemoPicture Picture, Rect Bounds)> Pictures);

    public override object CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<object> pictures,
        Func<InkStroke, Matrix3x2>? transformOf = null) => new Placement(
        strokes.Select(s => (s, transformOf?.Invoke(s) ?? s.PointTransform)).ToList(),
        pictures.Cast<DemoPicture>().Select(p => (p, p.Bounds)).ToList());

    public override void RestorePlacement(object placement)
    {
        var (strokes, places) = (Placement)placement;
        var current = CurrentStrokes(strokes.Select(s => s.Stroke));
        InkLog.Write($"demo: restore placement of {strokes.Count} stroke(s), {current.Count(s => s != null)} on the page: " +
            string.Join(", ", strokes.Select(s => $"({s.Transform.M11:0.##} {s.Transform.M31:0.#},{s.Transform.M32:0.#})")));
        for (int i = 0; i < current.Count; i++)
        {
            if (current[i] is { } stroke)
            {
                stroke.PointTransform = strokes[i].Transform;
            }
        }
        foreach (var (picture, bounds) in places)
        {
            picture.Bounds = bounds;
            Show(picture);
        }
    }

    // ---------------------------------------------------------------- pictures

    public override IEnumerable<object> Pictures => pictures;

    public override Rect PictureBounds(object picture) => ((DemoPicture)picture).Bounds;

    public override Action<Matrix3x2> PreviewPictures(IReadOnlyCollection<object> pictures)
    {
        var moved = pictures.Cast<DemoPicture>().Where(shown.ContainsKey).ToList();
        return m =>
        {
            foreach (var picture in moved)
            {
                // The page's transform in the element's own coordinates (its origin at the picture's corner).
                var at = Matrix3x2.CreateTranslation((float)picture.Bounds.X, (float)picture.Bounds.Y);
                Matrix3x2.Invert(at, out var back);
                var local = at * m * back;
                shown[picture].RenderTransform = new MatrixTransform
                {
                    Matrix = new Matrix(local.M11, local.M12, local.M21, local.M22, local.M31, local.M32),
                };
            }
        };
    }

    public override void TransformPictures(IReadOnlyCollection<object> pictures, Matrix3x2 m)
    {
        foreach (var picture in pictures.Cast<DemoPicture>())
        {
            picture.Bounds = Selection.TransformBounds(picture.Bounds, m);
            Show(picture);
        }
    }

    public override void AddPictures(IReadOnlyCollection<object> added)
    {
        foreach (var picture in added.Cast<DemoPicture>())
        {
            pictures.Add(picture);
            Show(picture);
        }
    }

    public override object RemovePictures(IReadOnlyCollection<object> removed)
    {
        var places = removed.Cast<DemoPicture>().Select(p => (Picture: p, Index: pictures.IndexOf(p)))
            .Where(p => p.Index >= 0).OrderBy(p => p.Index).ToList();
        foreach (var (picture, _) in places)
        {
            pictures.Remove(picture);
            if (shown.Remove(picture, out var border))
            {
                pictureLayer.Children.Remove(border);
            }
        }
        return places;
    }

    public override void RestorePictures(object removed)
    {
        foreach (var (picture, index) in (List<(DemoPicture Picture, int Index)>)removed)
        {
            pictures.Insert(Math.Min(index, pictures.Count), picture);
            Show(picture);
        }
        // Drawing order = list order.
        pictureLayer.Children.Clear();
        foreach (var picture in pictures)
        {
            pictureLayer.Children.Add(shown[picture]);
        }
    }

    // Places (and creates) the rectangle showing a picture; ends its preview.
    private void Show(DemoPicture picture)
    {
        if (!shown.TryGetValue(picture, out var border))
        {
            border = new Border
            {
                Background = new SolidColorBrush(Color.FromArgb(0x60, picture.Color.R, picture.Color.G, picture.Color.B)),
                BorderBrush = new SolidColorBrush(picture.Color),
                BorderThickness = new Thickness(1),
            };
            shown[picture] = border;
            pictureLayer.Children.Add(border);
        }
        border.RenderTransform = null;
        XamlCanvas.SetLeft(border, picture.Bounds.X);
        XamlCanvas.SetTop(border, picture.Bounds.Y);
        border.Width = picture.Bounds.Width;
        border.Height = picture.Bounds.Height;
    }
}
