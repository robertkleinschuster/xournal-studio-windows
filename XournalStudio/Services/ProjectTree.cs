namespace XournalStudio.Services;

/// <summary>A folder or document shown in the explorer.</summary>
public sealed record ProjectEntry(string Path, bool IsFolder, IReadOnlyList<ProjectEntry> Children)
{
    public string Name => System.IO.Path.GetFileName(Path);
}

/// <summary>
/// Reads the documents of a project folder: folders, .xopp and .pdf. A PDF is hidden when the .xopp of the same name
/// (its annotations) exists, and so are the companion PDFs of the GTK Xournal Studio. Folders without documents and
/// hidden folders (.git, ...) are left out.
/// </summary>
public static class ProjectTree
{
    private static readonly string[] HiddenPdfSuffixes = ["_kommentiert.pdf", ".pages.pdf", ".portals.pdf"];

    public static ProjectEntry Read(string folder) =>
        new(folder, true, ReadChildren(folder));

    public static bool IsShownFile(string path)
    {
        string name = Path.GetFileName(path);
        if (name.EndsWith(".xopp", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        return name.EndsWith(".pdf", StringComparison.OrdinalIgnoreCase) &&
            !HiddenPdfSuffixes.Any(s => name.EndsWith(s, StringComparison.OrdinalIgnoreCase)) &&
            !File.Exists(Path.ChangeExtension(path, ".xopp"));
    }

    private static List<ProjectEntry> ReadChildren(string folder)
    {
        var entries = new List<ProjectEntry>();
        IEnumerable<string> folders, files;
        try
        {
            folders = Directory.EnumerateDirectories(folder).Where(d => !IsHidden(d)).ToList();
            files = Directory.EnumerateFiles(folder).Where(IsShownFile).ToList();
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException)
        {
            return entries;
        }
        foreach (string sub in folders.Order(StringComparer.CurrentCultureIgnoreCase))
        {
            var children = ReadChildren(sub);
            if (children.Count > 0)
            {
                entries.Add(new ProjectEntry(sub, true, children));
            }
        }
        foreach (string file in files.Order(StringComparer.CurrentCultureIgnoreCase))
        {
            entries.Add(new ProjectEntry(file, false, []));
        }
        return entries;
    }

    private static bool IsHidden(string folder) =>
        Path.GetFileName(folder).StartsWith('.') ||
        new DirectoryInfo(folder).Attributes.HasFlag(FileAttributes.Hidden);
}
