using System.Numerics;
using Windows.UI.Input.Inking;

namespace InkPages;

/// <summary>How Windows ink draws a stroke, and strokes made from samples.</summary>
public static class InkStrokes
{
    private static readonly InkStrokeBuilder Builder = new();

    /// <summary>
    /// The width Windows ink draws a stroke with at a pressure: a stroke of size s at pressure p is about
    /// s * (0.25 + 1.5 p) wide (measured), i.e. exactly s at p = 0.5; without pressure its size.
    /// </summary>
    public static double DrawnWidth(InkDrawingAttributes attributes, float pressure) =>
        DrawnWidth(attributes.Size.Width, attributes.IgnorePressure, pressure);

    /// <summary><see cref="DrawnWidth(InkDrawingAttributes, float)"/> with the attributes' size and pressure mode read once.</summary>
    public static double DrawnWidth(double size, bool ignorePressure, float pressure) =>
        ignorePressure ? size : size * WidthFactor(pressure);

    /// <summary>The factor of <see cref="DrawnWidth"/> at a pressure.</summary>
    public static double WidthFactor(double pressure) => 0.25 + 1.5 * pressure;

    /// <summary>A stroke through these samples with these attributes (what the partial eraser leaves of a stroke).</summary>
    public static InkStroke FromSamples(IEnumerable<InkSample> samples, InkDrawingAttributes attributes)
    {
        Builder.SetDefaultDrawingAttributes(attributes);
        var stroke = Builder.CreateStrokeFromInkPoints(samples.Select(s => new InkPoint(s.Position, s.Pressure)), Matrix3x2.Identity);
        stroke.DrawingAttributes = attributes;
        return stroke;
    }
}
