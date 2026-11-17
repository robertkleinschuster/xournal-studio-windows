using System.Numerics;
using System.Xml.Linq;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace XournalStudio.Model;

/// <summary>
/// The elements of a page's strokes drawn in this session (they have no element from the file): each is converted once
/// (<see cref="StrokeConverter.ToXml"/>, about 0.1 ms a stroke) and reused by every later snapshot (saving, recovery copy
/// every few seconds, export) while the stroke is written with the same transform. A stroke's points and drawing
/// attributes never change; its transform does (moved, scaled, undone) – then it is converted again.
/// </summary>
internal sealed class ConvertedStrokes
{
    private readonly Dictionary<uint, (Matrix3x2 Transform, XElement Element)> elements = new();

    /// <summary>How many strokes were converted so far (for tests).</summary>
    internal int Conversions { get; private set; }

    /// <summary>The stroke's element written with <paramref name="transform"/>, as a new copy the caller may keep.</summary>
    public XElement ElementOf(InkStroke stroke, Matrix3x2 transform)
    {
        uint id = stroke.Id;
        if (!elements.TryGetValue(id, out var cached) || cached.Transform != transform)
        {
            cached = (transform, StrokeConverter.ToXml(stroke, transform));
            elements[id] = cached;
            Conversions++;
        }
        return new XElement(cached.Element);
    }

    /// <summary>Forgets the strokes no longer on the page (erased, deleted, replaced by an undo's copy).</summary>
    public void KeepOnly(IReadOnlySet<uint> live)
    {
        foreach (uint id in elements.Keys.Where(id => !live.Contains(id)).ToList())
        {
            elements.Remove(id);
        }
    }
}
