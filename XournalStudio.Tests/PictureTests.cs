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
        var moved = XoppImage.WithBounds(e, new Rect(30, 40, 100, 50));
        Assert.Equal("30", (string?)moved.Attribute("left"));
        Assert.Equal("90", (string?)moved.Attribute("bottom"));
        Assert.Equal(e.Value, moved.Value);
        Assert.Equal("10", (string?)e.Attribute("left")); // the original is unchanged
    }

    [Fact]
    public void MovedPicturesKeepAllTheirAttributes()
    {
        var e = XElement.Parse("""<teximage text="" left="1" top="2" right="3" bottom="4" natural_size="2 2" portal="abc">JVBERi0=</teximage>""");
        var moved = XoppImage.WithBounds(e, new Rect(10, 20, 2, 2));
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
        page.AddImage(added);
        Assert.Equal(110, page.ContentBottom());
        page.RemoveImage(page.Images.First());
        Assert.Equal(2, page.Images.Count());
        Assert.Equal(2, changes);
        Assert.Same(added, page.ToXml().Elements("layer").Last().Elements().Last());
    }
}
