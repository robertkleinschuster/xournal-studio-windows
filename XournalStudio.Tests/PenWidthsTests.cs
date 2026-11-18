using Xunit;

namespace XournalStudio.Tests;

public class PenWidthsTests
{
    [Theory]
    [InlineData(1, 0.42)]
    [InlineData(3, 1.41)]
    [InlineData(6, 5.67)]
    [InlineData(0, 0.42)]
    [InlineData(9, 5.67)]
    [InlineData(2.4, 0.85)]
    public void StepsAreXournalWidths(double step, double width) => Assert.Equal(width, PenWidths.Of(step));

    [Theory]
    [InlineData(1.0, 2)]
    [InlineData(1.41, 3)]
    [InlineData(0.3, 1)]
    [InlineData(10, 6)]
    public void SavedWidthsGoToTheNearestStep(double width, int step) => Assert.Equal(step, PenWidths.StepOf(width));

    [Fact]
    public void EveryStepRoundTrips()
    {
        for (int step = 1; step <= PenWidths.Steps.Count; step++)
        {
            Assert.Equal(step, PenWidths.StepOf(PenWidths.Of(step)));
        }
    }
}
