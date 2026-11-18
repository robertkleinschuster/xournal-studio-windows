namespace InkPages;

/// <summary>
/// The pens' widths as steps of the pen flyout's width slider. WinUI's stroke width slider moves in whole steps and
/// shows whole numbers, so a width below 1 cannot be chosen on it directly; the custom pen (<see cref="BallpointPen"/>,
/// InkToolbar's extension point for what a width means) maps the slider's steps 1..n to these widths – Xournal++'s pen
/// widths (very fine, fine, medium, thick) and two thicker ones.
/// </summary>
public static class PenWidths
{
    public static readonly IReadOnlyList<double> Steps = new[] { 0.42, 0.85, 1.41, 2.26, 3.4, 5.67 };

    /// <summary>The width of a slider step (1..n; rounded and clamped).</summary>
    public static double Of(double step) =>
        Steps[Math.Clamp((int)Math.Round(step, MidpointRounding.AwayFromZero), 1, Steps.Count) - 1];

    /// <summary>The slider step whose width is nearest to a width (1..n).</summary>
    public static int StepOf(double width)
    {
        int best = 0;
        for (int i = 1; i < Steps.Count; i++)
        {
            if (Math.Abs(Steps[i] - width) < Math.Abs(Steps[best] - width))
            {
                best = i;
            }
        }
        return best + 1;
    }
}
