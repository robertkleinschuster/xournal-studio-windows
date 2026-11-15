namespace XournalStudio.Services;

/// <summary>A folder or file shown in the explorer.</summary>
public sealed record ProjectEntry(string Path, bool IsFolder, IReadOnlyList<ProjectEntry> Children)
{
    public string Name => System.IO.Path.GetFileName(Path);
}

/// <summary>
/// Reads a project folder as it is on disk: every folder and file, sorted by name, nothing paired or filtered by the
/// app. Only what Windows itself marks hidden or system (as File Explorer's default view; e.g. .git) is left out.
/// Links (junctions, symbolic links) are shown but not entered: one pointing to a parent would recurse forever.
/// </summary>
public static class ProjectTree
{
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
        foreach (var sub in folders.OrderBy(d => d.Name, StringComparer.CurrentCultureIgnoreCase))
        {
            entries.Add(new ProjectEntry(sub.FullName, true, sub.LinkTarget != null ? [] : ReadChildren(sub.FullName)));
        }
        foreach (var file in files.OrderBy(f => f.Name, StringComparer.CurrentCultureIgnoreCase))
        {
            entries.Add(new ProjectEntry(file.FullName, false, []));
        }
        return entries;
    }

    private static bool IsShown(FileSystemInfo info) =>
        (info.Attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0;
}
