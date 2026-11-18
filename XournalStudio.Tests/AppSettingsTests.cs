using System.Text.Json;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

/// <summary>The toolbar's choices in the settings: the library's type as one object, older files read once.</summary>
public class AppSettingsTests
{
    [Fact]
    public void ToolChoicesAreKeptAsTheLibrarysObject()
    {
        var settings = new AppSettings();
        settings.Keep(new InkPages.ToolChoices
        {
            Pens = [new(1, 0.85), new(3, 2.26)],
            Highlighters = [new(0, 8.5)],
            Shape = InkPages.ShapeKind.Ellipse,
            EraserRadius = 7,
            WholeStrokeErasing = true,
            RecognizeShapes = true,
        });

        var read = JsonSerializer.Deserialize<AppSettings>(JsonSerializer.Serialize(settings))!.ToolChoices();

        Assert.Equal([new(1, 0.85), new(3, 2.26)], read.Pens);
        Assert.Equal([new InkPages.PenPreset(0, 8.5)], read.Highlighters);
        Assert.Equal(InkPages.ShapeKind.Ellipse, read.Shape);
        Assert.Equal(7, read.EraserRadius);
        Assert.True(read.WholeStrokeErasing);
        Assert.True(read.RecognizeShapes);
    }

    [Fact]
    public void ASettingsFileOfBeforeKeepsItsFlatChoices()
    {
        var old = JsonSerializer.Deserialize<AppSettings>(
            """{"PenStrokeWidth":1.41,"Pens":[{"ColorIndex":2,"Width":0.85}],"EraserRadius":6,"WholeStrokeErasing":true}""")!;

        var choices = old.ToolChoices();

        Assert.Null(old.Tools);
        Assert.Equal([new InkPages.PenPreset(2, 0.85)], choices.Pens);
        Assert.Equal(6, choices.EraserRadius);
        Assert.True(choices.WholeStrokeErasing);
    }
}
