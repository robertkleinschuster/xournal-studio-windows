using System.Numerics;
using System.Xml.Linq;
using XournalStudio.Model;
using Xunit;

namespace XournalStudio.Tests;

/// <summary>Strokes drawn in a session are converted to elements once, and every snapshot writes them exactly.</summary>
public class SnapshotTests
{
    private static XoppPage EmptyPage() => XoppPage.FromXml(XElement.Parse(
        """<page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer/></page>"""));

    private static Windows.UI.Input.Inking.InkStroke Stroke(string xml) => StrokeConverter.FromXml(XElement.Parse(xml))!;

    private static List<XElement> Strokes(XElement page) => page.Elements("layer").Single().Elements("stroke").ToList();

    private static void AssertSame(IEnumerable<XElement> expected, IEnumerable<XElement> actual)
    {
        var e = expected.ToList();
        var a = actual.ToList();
        Assert.Equal(e.Count, a.Count);
        for (int i = 0; i < e.Count; i++)
        {
            Assert.True(XNode.DeepEquals(e[i], a[i]), $"{e[i]} != {a[i]}");
        }
    }

    [Fact]
    public void DrawnStrokesAreConvertedOncePerTransformAndWrittenExactly()
    {
        var page = EmptyPage();
        var a = Stroke("""<stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke>""");
        var b = Stroke("""<stroke tool="highlighter" color="#ffff007f" width="8">5 5 50 50</stroke>""");

        var first = page.SnapshotOf([a, b]);
        AssertSame([StrokeConverter.ToXml(a), StrokeConverter.ToXml(b)], Strokes(first));
        Assert.Equal(2, page.StrokeConversions);

        var second = page.SnapshotOf([a, b]);
        Assert.True(XNode.DeepEquals(first, second));
        Assert.Equal(2, page.StrokeConversions); // reused, not converted again

        // A snapshot's elements are its own: changing one changes no later snapshot.
        Strokes(second)[0].Value = "1 1 2 2";
        Strokes(second)[0].SetAttributeValue("color", "#000000ff");
        Assert.True(XNode.DeepEquals(first, page.SnapshotOf([a, b])));

        // Drawing order is the ink's.
        AssertSame([StrokeConverter.ToXml(b), StrokeConverter.ToXml(a)], Strokes(page.SnapshotOf([b, a])));
        Assert.Equal(2, page.StrokeConversions);
    }

    [Fact]
    public void AMovedOrPreviewedStrokeIsWrittenWithItsTransform()
    {
        var page = EmptyPage();
        var a = Stroke("""<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>""");
        page.SnapshotOf([a]);

        a.PointTransform = Matrix3x2.CreateTranslation(100, 0); // committed move
        var moved = Strokes(page.SnapshotOf([a])).Single();
        Assert.True(XNode.DeepEquals(StrokeConverter.ToXml(a), moved));
        Assert.StartsWith("100 0 ", moved.Value);
        Assert.Equal(2, page.StrokeConversions);

        // A move previewed on it: written with the committed transform until the preview ends.
        page.BeginPreview(a, a.PointTransform);
        a.PointTransform = Matrix3x2.CreateTranslation(300, 0);
        Assert.StartsWith("100 0 ", Strokes(page.SnapshotOf([a])).Single().Value);
        Assert.Equal(2, page.StrokeConversions); // the committed transform is the one converted already
        page.EndPreview([a]);
        Assert.StartsWith("300 0 ", Strokes(page.SnapshotOf([a])).Single().Value);

        // Undo puts the old transform back: the file says so again.
        a.PointTransform = Matrix3x2.Identity;
        Assert.True(XNode.DeepEquals(StrokeConverter.ToXml(a), Strokes(page.SnapshotOf([a])).Single()));
        Assert.StartsWith("0 0 ", Strokes(page.SnapshotOf([a])).Single().Value);
    }

    [Fact]
    public void StrokesNoLongerOnTheInkAreLeftOut()
    {
        var page = EmptyPage();
        var a = Stroke("""<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>""");
        var b = Stroke("""<stroke tool="pen" color="#000000ff" width="1">0 5 10 5</stroke>""");
        page.SnapshotOf([a, b]);
        AssertSame([StrokeConverter.ToXml(b)], Strokes(page.SnapshotOf([b])));
        AssertSame([StrokeConverter.ToXml(a), StrokeConverter.ToXml(b)], Strokes(page.SnapshotOf([a, b])));
    }

    [Fact]
    public void AReleasedViewsInkIsWrittenIntoThePageAsASnapshotWritesIt()
    {
        var page = XoppPage.FromXml(XElement.Parse("""
            <page width="200" height="200"><background type="solid" color="#ffffffff" style="plain"/><layer>
            <stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>
            <stroke tool="pen" color="#000000ff" width="1" other="kept">0 5 10 5</stroke>
            <stroke tool="pen" color="#000000ff" width="1" style="dash">0 9 10 9</stroke>
            </layer></page>
            """));
        var read = page.ReadStrokes(); // the dashed one is drawn from the file, not ink
        Assert.Equal(2, read.Count);
        page.Adopt(read); // as a view's ink
        var drawn = Stroke("""<stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke>""");
        drawn.PointTransform = Matrix3x2.CreateTranslation(5, 0);
        var pasted = Stroke("""<stroke tool="pen" color="#00ff00ff" width="1" pasted="yes">1 1 2 2</stroke>""");
        var pastedElement = XElement.Parse("""<stroke tool="pen" color="#00ff00ff" width="1" pasted="yes">1 1 2 2</stroke>""");
        page.Adopt([(pasted, pastedElement)]); // pasted: its element, not in the page yet
        List<Windows.UI.Input.Inking.InkStroke> ink = [read[1].Stroke, drawn, pasted]; // the first one erased

        var expected = page.SnapshotOf(ink);
        page.WriteBack(ink);
        Assert.True(XNode.DeepEquals(expected, page.Snapshot()), page.Snapshot().ToString());

        // The next view reads exactly these strokes again: the kept one, the drawn one as written, the pasted one.
        var again = page.ReadStrokes();
        Assert.Equal(3, again.Count);
        Assert.Equal("kept", (string?)again[0].Element.Attribute("other"));
        Assert.StartsWith("5 0 ", again[1].Element.Value);
        Assert.Equal("yes", (string?)again[2].Element.Attribute("pasted"));
        Assert.Single(page.DrawnStrokes); // the dashed one stays as it is
    }

    [Theory]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="1">0 0 10 0</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2 1 3 4">0 0 10 0 10 10 0 10</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2">5 5</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2"></stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2" fill="128">0 0 10 0 10 10</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2" style="dash">0 0 10 0</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2">0 0 10 NaN</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2 x">0 0 10 0</stroke>""")]
    [InlineData("""<stroke tool="pen" color="#000000ff" width="2">0 0 10 0 7</stroke>""")]
    public void InkOfIsNeedsOwnDrawingThenFromXml(string xml)
    {
        var e = XElement.Parse(xml);
        var expected = StrokeConverter.NeedsOwnDrawing(e) ? null : StrokeConverter.FromXml(e);
        var actual = StrokeConverter.InkOf(e);
        Assert.Equal(expected == null, actual == null);
        if (expected != null)
        {
            Assert.True(XNode.DeepEquals(StrokeConverter.ToXml(expected), StrokeConverter.ToXml(actual!)));
        }
    }

    [Fact]
    public async Task ADocumentLoadedOnTheThreadPoolGivesInkUsableHere()
    {
        string path = Path.Combine(Path.GetTempPath(), $"snapshot-test-{Guid.NewGuid()}.xopp");
        try
        {
            var doc = XoppDocument.CreateNew();
            await doc.SaveAsync(path);
            File.WriteAllText(path, """
                <?xml version="1.0" standalone="no"?>
                <xournal creator="test" fileversion="4"><page width="100" height="100">
                <background type="solid" color="#ffffffff" style="plain"/>
                <layer><stroke tool="pen" color="#ff0000ff" width="2 1 3">0 0 10 5 20 0</stroke></layer></page></xournal>
                """);
            var loaded = await Task.Run(() => XoppDocument.Load(path));
            var stroke = loaded.Pages.Single().Strokes.Single();
            Assert.Equal(3, stroke.GetInkPoints().Count);
            Assert.Equal(2, stroke.DrawingAttributes.Size.Width);
            // And the builder of this thread still works while another thread's built strokes.
            Assert.NotNull(StrokeConverter.FromXml(XElement.Parse("""<stroke tool="pen" color="#000000ff" width="1">0 0 1 1</stroke>""")));
        }
        finally
        {
            File.Delete(path);
        }
    }
}
