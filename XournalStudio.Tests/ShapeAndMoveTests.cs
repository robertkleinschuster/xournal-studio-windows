using System.Xml.Linq;
using Windows.Foundation;
using XournalStudio.Model;
using Xunit;

namespace XournalStudio.Tests;

public class ShapeAndMoveTests
{
    [Fact]
    public void RectangleIsClosedThroughAllCorners()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Rectangle, new Point(10, 20), new Point(50, 40), 1.41);
        Assert.Equal([new(10, 20), new(50, 20), new(50, 40), new(10, 40), new(10, 20)], outline);
    }

    [Fact]
    public void ArrowHeadSitsAtTheEnd()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Arrow, new Point(0, 0), new Point(100, 0), 1.41);
        Assert.Equal(5, outline.Count);
        Assert.Equal(new Point(100, 0), outline[1]);
        Assert.Equal(new Point(100, 0), outline[3]);
        Assert.True(outline[2].X < 100 && outline[4].X < 100);
        Assert.Equal(-outline[2].Y, outline[4].Y, 6); // one side above, one below the shaft
        Assert.NotEqual(0, outline[2].Y);
    }

    [Fact]
    public void EllipseFitsTheDraggedRectangle()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Ellipse, new Point(0, 0), new Point(40, 20), 1.41);
        Assert.Equal(outline[0], outline[^1]);
        Assert.Equal(40, outline.Max(p => p.X), 6);
        Assert.Equal(0, outline.Min(p => p.X), 6);
        Assert.Equal(20, outline.Max(p => p.Y), 1);
    }

    [Fact]
    public void ShapeStrokeHasEvenWidthWithoutSmoothing()
    {
        var pen = new Windows.UI.Input.Inking.InkDrawingAttributes { Size = new Size(2.26, 2.26) };
        var stroke = ShapeGeometry.CreateStroke(ShapeGeometry.Outline(ShapeKind.Line, new(0, 0), new(30, 0), 2.26), pen);
        Assert.True(stroke.DrawingAttributes.IgnorePressure);
        Assert.False(stroke.DrawingAttributes.FitToCurve);
        Assert.Equal(2, stroke.GetInkPoints().Count);
    }

    [Fact]
    public void TranslateMovesPointsAndKeepsUnknownAttributes()
    {
        var e = XElement.Parse("""<stroke tool="pen" color="#000000ff" width="1.41 1.2" fork-extra="keep">10 20 30.5 40</stroke>""");
        StrokeConverter.Transform(e, System.Numerics.Matrix3x2.CreateTranslation(5, -2.5f));
        Assert.Equal("15 17.5 35.5 37.5", e.Value);
        Assert.Equal("keep", (string?)e.Attribute("fork-extra"));
        Assert.Equal("1.41 1.2", (string?)e.Attribute("width"));
    }

    [Fact]
    public void ScalingAboutACornerScalesPointsButNotWidths()
    {
        var e = XElement.Parse("""<stroke tool="pen" color="#000000ff" width="1.41 1.2">10 10 30 20</stroke>""");
        var m = System.Numerics.Matrix3x2.CreateScale(2, new System.Numerics.Vector2(10, 10));
        StrokeConverter.Transform(e, m);
        Assert.Equal("10 10 50 30", e.Value);
        Assert.Equal("1.41 1.2", (string?)e.Attribute("width"));
        Assert.Equal(new Rect(10, 10, 40, 20), XoppImage.Transform(new Rect(10, 10, 20, 10), m));
    }

    [Fact]
    public void SetBoundsMovesThePictureInPlace()
    {
        var layer = XElement.Parse("""<layer><image left="1" top="2" right="11" bottom="22" extra="x">AAAA</image><stroke/></layer>""");
        var image = layer.Element("image")!;
        XoppImage.SetBounds(image, new Rect(6, 7, 10, 20));
        Assert.Same(image, layer.Elements().First()); // same element, same place in the layer
        Assert.Equal(new Rect(6, 7, 10, 20), XoppImage.GetBounds(image));
        Assert.Equal("x", (string?)image.Attribute("extra"));
    }

    [Fact]
    public void ClipIsRelativeToItsCornerAndPlacesScaled()
    {
        var stroke = XElement.Parse("""<stroke tool="pen" color="#000000ff" width="1.41 1.2" extra="x">110 220 130 240</stroke>""");
        var image = XElement.Parse("""<image left="100" top="200" right="140" bottom="220">AAAA</image>""");
        var clip = XoppClip.Create(new Rect(100, 200, 40, 40), [image, stroke]);

        Assert.Equal(new Size(40, 40), XoppClip.GetSize(clip));
        Assert.Equal("110 220 130 240", stroke.Value); // the originals stay where they are

        var placed = XoppClip.Place(clip, System.Numerics.Matrix3x2.CreateScale(0.5f) * System.Numerics.Matrix3x2.CreateTranslation(14, 28));
        Assert.Equal(new Rect(14, 28, 20, 10), XoppImage.GetBounds(placed[0]));
        Assert.Equal("19 38 29 48", placed[1].Value);
        Assert.Equal("x", (string?)placed[1].Attribute("extra"));
        Assert.Equal("1.41 1.2", (string?)placed[1].Attribute("width"));
    }
}
