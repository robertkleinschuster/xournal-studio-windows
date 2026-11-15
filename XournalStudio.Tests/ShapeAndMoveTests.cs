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
        StrokeConverter.Translate(e, 5, -2.5);
        Assert.Equal("15 17.5 35.5 37.5", e.Value);
        Assert.Equal("keep", (string?)e.Attribute("fork-extra"));
        Assert.Equal("1.41 1.2", (string?)e.Attribute("width"));
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
}
