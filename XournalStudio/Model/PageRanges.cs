using System.Globalization;

namespace XournalStudio.Model;

/// <summary>Page numbers as people type them: "1", "1, 3-5", "2-" (to the end); empty = all pages.</summary>
public static class PageRanges
{
    /// <summary>
    /// The 0-based page indices of <paramref name="text"/> in the order given (duplicates kept: a page can be inserted
    /// twice), or the problem in German for the dialog.
    /// </summary>
    public static (List<int>? Pages, string? Problem) Parse(string? text, int pageCount)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return (Enumerable.Range(0, pageCount).ToList(), null);
        }
        var pages = new List<int>();
        foreach (var part in text.Split(',', ';').Select(p => p.Trim()).Where(p => p.Length > 0))
        {
            var ends = part.Split('-', '–').Select(e => e.Trim()).ToArray();
            if (ends.Length > 2 || !TryNumber(ends[0], out int first))
            {
                return (null, $"„{part}“ ist keine Seitenangabe.");
            }
            int last = first;
            if (ends.Length == 2 && !(ends[1].Length == 0 ? (last = pageCount) > 0 : TryNumber(ends[1], out last)))
            {
                return (null, $"„{part}“ ist keine Seitenangabe.");
            }
            if (first < 1 || last > pageCount || first > last)
            {
                return (null, $"„{part}“: Das PDF hat die Seiten 1 bis {pageCount}.");
            }
            pages.AddRange(Enumerable.Range(first - 1, last - first + 1));
        }
        return pages.Count == 0 ? (null, "Keine Seite angegeben.") : (pages, null);
    }

    private static bool TryNumber(string s, out int n) => int.TryParse(s, NumberStyles.None, CultureInfo.InvariantCulture, out n);
}
