using Windows.Foundation;
using XournalStudio.Model;
using Xunit;

namespace XournalStudio.Tests;

public class StrokeEraserTests
{
    private static List<InkSample> Line(params double[] xy) =>
        Enumerable.Range(0, xy.Length / 2).Select(i => new InkSample(new Point(xy[2 * i], xy[2 * i + 1]), 0.5f)).ToList();

    private static double Half(InkSample _) => 1; // a stroke 2 pt wide

    [Fact]
    public void CircleInTheMiddleCutsTheStrokeWhereTheInkEdgeMeetsIt()
    {
        // Horizontal line 0..100 at y=0, circle r=5 at x=50: the ink edge (half width 1) stops at the circle,
        // so the centerline ends 5 + 1 = 6 away from the center.
        var pieces = StrokeEraser.Erase(Line(0, 0, 100, 0), Half, new Point(50, 0), 5)!;
        Assert.Equal(2, pieces.Count);
        Assert.Equal(44, pieces[0][^1].Position.X, 9);
        Assert.Equal(56, pieces[1][0].Position.X, 9);
        Assert.Equal(new Point(0, 0), pieces[0][0].Position);
        Assert.Equal(new Point(100, 0), pieces[1][^1].Position);
    }

    [Fact]
    public void InkJustOutsideTheCircleStays()
    {
        // The ink edge at y=6-1=5 touches a circle of radius 5 at the origin only: nothing is erased.
        Assert.Null(StrokeEraser.Erase(Line(-20, 6, 20, 6), Half, new Point(0, 0), 5));
        // A little closer it is cut.
        Assert.NotNull(StrokeEraser.Erase(Line(-20, 5.9, 20, 5.9), Half, new Point(0, 0), 5));
    }

    [Fact]
    public void ErasingAnEndShortensAndErasingAllRemovesIt()
    {
        var shortened = StrokeEraser.Erase(Line(0, 0, 10, 0, 20, 0), Half, new Point(0, 0), 4)!;
        Assert.Equal(5, Assert.Single(shortened)[0].Position.X, 9);
        Assert.Empty(StrokeEraser.Erase(Line(0, 0, 3, 0), Half, new Point(1, 0), 10)!);
    }

    [Fact]
    public void PressureIsInterpolatedAtTheCut()
    {
        var samples = new List<InkSample> { new(new Point(0, 0), 0.2f), new(new Point(100, 0), 0.6f) };
        var pieces = StrokeEraser.Erase(samples, Half, new Point(100, 0), 49)!; // cut at x = 50
        Assert.Equal(0.4f, Assert.Single(pieces)[^1].Pressure, 4);
    }

    [Fact]
    public void ASegmentInsideOnlyForItsWiderNeighbourKeepsItsStart()
    {
        // a is wide (half width 6), b and c thin (1): the circle (r 2 at 0,0) reaches b only through a's width.
        var samples = new List<InkSample> { new(new Point(-10, 0), 1f), new(new Point(7, 0), 0f), new(new Point(30, 0), 0f) };
        double Half(InkSample s) => s.Pressure > 0.5 ? 6 : 1;
        var pieces = StrokeEraser.Erase(samples, Half, new Point(0, 0), 2)!;
        var last = pieces[^1];
        Assert.Equal(new Point(30, 0), last[^1].Position);
        Assert.Equal(7, last[0].Position.X, 9); // b→c (outside for its own width) is kept from b on
    }
}
