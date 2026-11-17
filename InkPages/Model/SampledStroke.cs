using System.Numerics;
using Windows.Foundation;
using Windows.UI.Input.Inking;

namespace InkPages;

/// <summary>
/// A stroke as the partial eraser cuts it (<see cref="StrokeEraser"/>): its samples in page points (its transform
/// applied) and its drawn half width, read from the ink once – a drag erases with many pointer events, each over the
/// same strokes.
/// </summary>
public sealed class SampledStroke
{
    private SampledStroke(List<InkSample> samples, InkDrawingAttributes attributes)
    {
        Samples = samples;
        Attributes = attributes;
        double size = attributes.Size.Width;
        bool ignorePressure = attributes.IgnorePressure;
        HalfWidth = s => InkStrokes.DrawnWidth(size, ignorePressure, s.Pressure) / 2;
    }

    public List<InkSample> Samples { get; }

    public InkDrawingAttributes Attributes { get; }

    /// <summary>Half the drawn width at a sample (its attributes read once).</summary>
    public Func<InkSample, double> HalfWidth { get; }

    /// <summary>Reads a stroke of the ink.</summary>
    public static SampledStroke Of(InkStroke stroke)
    {
        var transform = stroke.PointTransform;
        var samples = stroke.GetInkPoints().Select(p =>
        {
            var at = Vector2.Transform(new((float)p.Position.X, (float)p.Position.Y), transform);
            return new InkSample(new Point(at.X, at.Y), p.Pressure);
        }).ToList();
        return new SampledStroke(samples, stroke.DrawingAttributes);
    }

    /// <summary>
    /// A stroke made by <see cref="InkStrokes.FromSamples"/> from <paramref name="samples"/>: what <see cref="Of"/>
    /// would read from it (the ink keeps its points as floats, without a transform), without reading it.
    /// </summary>
    public static SampledStroke Made(IEnumerable<InkSample> samples, InkDrawingAttributes attributes) => new(
        samples.Select(s => new InkSample(new Point((float)s.Position.X, (float)s.Position.Y), s.Pressure)).ToList(),
        attributes);
}
