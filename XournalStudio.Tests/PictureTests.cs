using System.Xml.Linq;
using Windows.Foundation;
using XournalStudio.Model;
using Xunit;

namespace XournalStudio.Tests;

public class PictureTests
{
    [Fact]
    public void ImageElementsAreXournalImages()
    {
        var e = XoppImage.Create(new Rect(10, 20, 100, 50), [0x89, 0x50, 0x4e, 0x47], 300, 150);
        Assert.Equal("image", e.Name);
        Assert.Equal(new Rect(10, 20, 100, 50), XoppImage.GetBounds(e));
        Assert.Equal([0x89, 0x50, 0x4e, 0x47], XoppImage.GetData(e));
        var moved = XoppClip.Transformed(e, System.Numerics.Matrix3x2.CreateTranslation(20, 20));
        Assert.Equal("30", (string?)moved.Attribute("left"));
        Assert.Equal("90", (string?)moved.Attribute("bottom"));
        Assert.Equal(e.Value, moved.Value);
        Assert.Equal("10", (string?)e.Attribute("left")); // the original is unchanged
    }

    [Fact]
    public void MovedPicturesKeepAllTheirAttributes()
    {
        var e = XElement.Parse("""<teximage text="" left="1" top="2" right="3" bottom="4" natural_size="2 2" portal="abc">JVBERi0=</teximage>""");
        var moved = XoppClip.Transformed(e, System.Numerics.Matrix3x2.CreateTranslation(9, 18));
        Assert.Equal(["text", "left", "top", "right", "bottom", "natural_size", "portal"], moved.Attributes().Select(a => a.Name.LocalName));
        Assert.Equal("22", (string?)moved.Attribute("bottom"));
        Assert.Equal("JVBERi0=", moved.Value);
    }

    [Fact]
    public void PagesListAndChangeTheirImages()
    {
        var page = XoppPage.FromXml(XElement.Parse("""
            <page width="595" height="842"><background type="solid" color="#ffffffff" style="graph"/>
            <layer><teximage left="1" top="2" right="3" bottom="4">JVBERi0=</teximage></layer>
            <layer><image left="5" top="10" right="50" bottom="60">iVBO</image></layer></page>
            """));
        Assert.Equal(2, page.Images.Count());
        Assert.Equal(60, page.ContentBottom());
        Assert.True(XoppImage.IsPdf(XoppImage.GetData(page.Images.First())));

        int changes = 0;
        page.ImagesChanged += (_, _) => changes++;
        var added = XoppImage.Create(new Rect(0, 100, 10, 10), [1], 1, 1);
        page.AddImages([added]);
        Assert.Equal(110, page.ContentBottom());
        page.RemoveImages([page.Images.First()]);
        Assert.Equal(2, page.Images.Count());
        Assert.Equal(2, changes);
        Assert.True(XNode.DeepEquals(added, page.Snapshot().Elements("layer").Last().Elements().Last())); // a copy
    }

    [Fact]
    public void RemovedPicturesComeBackAtTheirPlace()
    {
        var page = XoppPage.FromXml(XElement.Parse("""
            <page width="100" height="100"><background type="solid" color="#ffffffff" style="graph"/>
            <layer><image left="0" top="0" right="1" bottom="1">AA==</image><stroke tool="pen" color="#000000ff" width="1">0 0 1 1</stroke></layer>
            <layer name="2"><stroke tool="pen" color="#000000ff" width="1">0 0 2 2</stroke></layer></page>
            """));
        var image = page.Images.Single();
        var places = page.RemoveImages([image]);
        Assert.Empty(page.Images);
        page.RestoreImages(places);
        var layer = page.Snapshot().Elements("layer").First();
        Assert.Equal("image", layer.Elements().First().Name.LocalName); // first in the first layer again, under the stroke
    }
}
