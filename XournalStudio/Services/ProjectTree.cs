namespace XournalStudio.Services;

/// <summary>A folder or document shown in the explorer.</summary>
public sealed record ProjectEntry(string Path, bool IsFolder, IReadOnlyList<ProjectEntry> Children)
{
    public string Name => System.IO.Path.GetFileName(Path);
}

/// <summary>
/// Reads the documents of a project folder: folders, .xopp and .pdf. A PDF is hidden when the .xopp of the same name
/// (its annotations) exists, and so are upstream's attached PDFs and the companion PDFs of the GTK Xournal Studio.
/// Folders with content but no documents and hidden folders (.git, ...) are left out; an empty folder is shown (just
/// created, documents are about to go in).
/// </summary>
public static class ProjectTree
{
    private static readonly string[] HiddenPdfSuffixes = ["_kommentiert.pdf", ".pages.pdf", ".portals.pdf", ProjectFiles.AttachedPdfSuffix];

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
            DebugLog.Write($"read folder {folder} (left out)", e);
            return entries;
        }
        foreach (string sub in folders.Order(StringComparer.CurrentCultureIgnoreCase))
        {
            var children = ReadChildren(sub);
            if (children.Count > 0 || IsEmpty(sub))
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

    private static bool IsEmpty(string folder)
    {
        try
        {
            return !Directory.EnumerateFileSystemEntries(folder).Any();
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException)
        {
            DebugLog.Write($"read folder {folder} (left out)", e);
            return false;
        }
    }

    // Junctions and symbolic links are skipped too: one pointing to a parent folder would recurse forever.
    // (Not all reparse points: OneDrive's online-only folders are ones as well.)
    private static bool IsHidden(string folder)
    {
        var info = new DirectoryInfo(folder);
        return info.Name.StartsWith('.') || info.Attributes.HasFlag(FileAttributes.Hidden) || info.LinkTarget != null;
    }
}
