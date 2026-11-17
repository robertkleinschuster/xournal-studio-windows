using Windows.Foundation;

namespace InkPages;

/// <summary>A point of a stroke with its pen pressure (page points).</summary>
public readonly record struct InkSample(Point Position, float Pressure);

/// <summary>
/// The partial eraser's geometry (Windows ink only erases whole strokes; a deliberate addition): what is left
/// of a polyline stroke when a circle is erased from it. The cut is exact: each segment is intersected with the circle
/// grown by half the drawn width there, so the ink's edge – not its centerline – ends at the circle and nothing outside
/// it disappears. New end points get the pressure interpolated at the cut.
/// </summary>
public static class StrokeEraser
{
    private const double MinPieceLength = 0.01; // shorter remains would only be a dot

    /// <summary>
    /// The pieces left after erasing the circle (<paramref name="center"/>, <paramref name="radius"/>), or null if the
    /// circle does not touch the stroke. An empty list: all of it is erased.
    /// </summary>
    /// <param name="halfWidth">Half the drawn width at a sample (pressure-dependent for pressure strokes).</param>
    public static List<List<InkSample>>? Erase(IReadOnlyList<InkSample> samples, Func<InkSample, double> halfWidth,
        Point center, double radius)
    {
        if (samples.Count == 0)
        {
            return null;
        }
        var pieces = new List<List<InkSample>>();
        var current = new List<InkSample>();
        bool touched = false;

        if (samples.Count == 1)
        {
            // A dot.
            var only = samples[0];
            return Distance(only.Position, center) < radius + halfWidth(only) ? [] : null;
        }
        if (Distance(samples[0].Position, center) >= radius + halfWidth(samples[0]))
        {
            current.Add(samples[0]);
        }
        for (int i = 1; i < samples.Count; i++)
        {
            var a = samples[i - 1];
            var b = samples[i];
            double r = radius + Math.Max(halfWidth(a), halfWidth(b));
            if (current.Count == 0 && Distance(a.Position, center) >= r)
            {
                current.Add(a); // outside for this segment (the previous one, wider, ended it inside)
            }
            if (Inside(a.Position, b.Position, center, r) is not var (t1, t2))
            {
                current.Add(b);
                continue;
            }
            touched = true;
            if (t1 > 0)
            {
                current.Add(At(a, b, t1));
            }
            Close(current, pieces);
            current = [];
            if (t2 < 1)
            {
                current.Add(At(a, b, t2));
                current.Add(b);
            }
        }
        if (!touched)
        {
            return null;
        }
        Close(current, pieces);
        return pieces;
    }

    // The part [t1, t2] of segment a→b (t from 0 to 1) inside the circle, or null.
    private static (double, double)? Inside(Point a, Point b, Point c, double r)
    {
        double dx = b.X - a.X, dy = b.Y - a.Y;
        double fx = a.X - c.X, fy = a.Y - c.Y;
        double qa = dx * dx + dy * dy;
        double qc = fx * fx + fy * fy - r * r;
        if (qa < 1e-12)
        {
            return qc < 0 ? (0, 1) : null; // a zero-length segment: inside or not
        }
        double qb = 2 * (dx * fx + dy * fy);
        double disc = qb * qb - 4 * qa * qc;
        if (disc <= 0)
        {
            return null; // passes by (or only touches)
        }
        double root = Math.Sqrt(disc);
        double t1 = (-qb - root) / (2 * qa), t2 = (-qb + root) / (2 * qa);
        if (t2 <= 0 || t1 >= 1)
        {
            return null;
        }
        return (Math.Max(0, t1), Math.Min(1, t2));
    }

    private static InkSample At(InkSample a, InkSample b, double t) => new(
        new Point(a.Position.X + t * (b.Position.X - a.Position.X), a.Position.Y + t * (b.Position.Y - a.Position.Y)),
        (float)(a.Pressure + t * (b.Pressure - a.Pressure)));

    private static void Close(List<InkSample> piece, List<List<InkSample>> pieces)
    {
        double length = 0;
        for (int i = 1; i < piece.Count; i++)
        {
            length += Distance(piece[i - 1].Position, piece[i].Position);
        }
        if (piece.Count >= 2 && length >= MinPieceLength)
        {
            pieces.Add(piece);
        }
    }

    private static double Distance(Point a, Point b) => Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
}
