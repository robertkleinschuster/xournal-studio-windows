namespace XournalStudio.Services;

/// <summary>A folder or file shown in the explorer.</summary>
public sealed record ProjectEntry(string Path, bool IsFolder, IReadOnlyList<ProjectEntry> Children)
{
    public string Name => System.IO.Path.GetFileName(Path);
}

/// <summary>
/// Reads a project folder as it is on disk: every folder and file, sorted by name with numbers by their value
/// ("Vorlesung 2" before "Vorlesung 11", as File Explorer shows them – <see cref="NameOrder"/>), nothing paired or filtered by the
/// app. Only what Windows itself marks hidden or system (as File Explorer's default view; e.g. .git) is left out.
/// Links (junctions, symbolic links) are shown but not entered: one pointing to a parent would recurse forever.
/// </summary>
public static class ProjectTree
{
    /// <summary>
    /// Names in the user's language, case ignored, digits compared as numbers (.NET's <c>CompareOptions.NumericOrdering</c>);
    /// names equal so (only case differs) follow in ordinal order, so the tree keeps its order between refreshes.
    /// </summary>
    public static readonly StringComparer NameOrder = StringComparer.Create(System.Globalization.CultureInfo.CurrentCulture,
        System.Globalization.CompareOptions.IgnoreCase | System.Globalization.CompareOptions.NumericOrdering);

    public static ProjectEntry Read(string folder) =>
        new(folder, true, ReadChildren(folder));

    private static List<ProjectEntry> ReadChildren(string folder)
    {
        var entries = new List<ProjectEntry>();
        List<DirectoryInfo> folders;
        List<FileInfo> files;
        try
        {
            var dir = new DirectoryInfo(folder);
            folders = dir.EnumerateDirectories().Where(IsShown).ToList();
            files = dir.EnumerateFiles().Where(IsShown).ToList();
        }
        catch (Exception e) when (e is UnauthorizedAccessException or IOException)
        {
            DebugLog.Write($"read folder {folder} (shown empty)", e);
            return entries;
        }
        foreach (var sub in folders.OrderBy(d => d.Name, NameOrder).ThenBy(d => d.Name, StringComparer.Ordinal))
        {
            entries.Add(new ProjectEntry(sub.FullName, true, sub.LinkTarget != null ? [] : ReadChildren(sub.FullName)));
        }
        foreach (var file in files.OrderBy(f => f.Name, NameOrder).ThenBy(f => f.Name, StringComparer.Ordinal))
        {
            entries.Add(new ProjectEntry(file.FullName, false, []));
        }
        return entries;
    }

    private static bool IsShown(FileSystemInfo info) =>
        (info.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0;
}

