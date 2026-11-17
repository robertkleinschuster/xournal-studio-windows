using System.Numerics;
using Windows.Foundation;
using Windows.UI.Input.Inking;
using Xunit;

namespace XournalStudio.Tests;

/// <summary>The partial eraser reads each stroke once per drag; what it keeps must be what reading the ink gives.</summary>
public class SampledStrokeTests
{
    private static InkDrawingAttributes Attributes(bool ignorePressure) => new()
    {
        Size = new Size(3, 3),
        IgnorePressure = ignorePressure,
        FitToCurve = false,
    };

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void APieceMadeByTheEraserIsWhatReadingItGives(bool ignorePressure)
    {
        var random = new Random(7);
        for (int n = 0; n < 200; n++)
        {
            var samples = Enumerable.Range(0, random.Next(2, 30))
                .Select(_ => new InkSample(new Point(random.NextDouble() * 600, random.NextDouble() * 800), (float)random.NextDouble()))
                .ToList();
            var attributes = Attributes(ignorePressure);
            var made = SampledStroke.Made(samples, attributes);
            var read = SampledStroke.Of(InkStrokes.FromSamples(samples, attributes));
            Assert.Equal(read.Samples, made.Samples);
            Assert.All(read.Samples, s => Assert.Equal(read.HalfWidth(s), made.HalfWidth(s)));
        }
    }

    [Fact]
    public void ReadingAppliesTheTransformAndTheDrawnWidth()
    {
        var attributes = Attributes(ignorePressure: false);
        var stroke = InkStrokes.FromSamples([new(new Point(0, 0), 0.5f), new(new Point(10, 0), 1f)], attributes);
        stroke.PointTransform = Matrix3x2.CreateTranslation(5, 7);
        var read = SampledStroke.Of(stroke);
        Assert.Equal([new InkSample(new Point(5, 7), 0.5f), new InkSample(new Point(15, 7), 1f)], read.Samples);
        Assert.Equal(InkStrokes.DrawnWidth(stroke.DrawingAttributes, 1f) / 2, read.HalfWidth(read.Samples[1]));
        Assert.Equal(3 * 1.75 / 2, read.HalfWidth(read.Samples[1]), 6);
    }
}
