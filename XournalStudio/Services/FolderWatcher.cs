namespace XournalStudio.Services;

/// <summary>
/// Reports changes in the project folder (files and folders created, deleted, renamed or written) with .NET's
/// <see cref="FileSystemWatcher"/>; changes inside hidden folders such as .git are left out (git status itself writes
/// there). <see cref="Changed"/> is raised on a thread-pool thread, once per change: the receiver debounces.
/// </summary>
public sealed class FolderWatcher : IDisposable
{
    private readonly FileSystemWatcher watcher;

    public FolderWatcher(string folder)
    {
        Folder = folder;
        watcher = new FileSystemWatcher(folder)
        {
            IncludeSubdirectories = true,
            NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite,
        };
        watcher.Created += OnChanged;
        watcher.Deleted += OnChanged;
        watcher.Changed += OnChanged;
        watcher.Renamed += OnChanged;
        watcher.Error += OnError;
        watcher.EnableRaisingEvents = true;
    }

    public event EventHandler? Changed;

    public string Folder { get; }

    /// <summary>True for paths inside a folder whose name starts with a dot (.git, ...), which the explorer hides.</summary>
    public static bool IsInHiddenFolder(string folder, string path)
    {
        string relative = Path.GetRelativePath(folder, path);
        var parts = relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return parts.Take(parts.Length - 1).Any(p => p.StartsWith('.') && p != "..") ||
            (parts[^1].StartsWith('.') && Directory.Exists(path));
    }

    private void OnChanged(object sender, FileSystemEventArgs e)
    {
        if (!IsInHiddenFolder(Folder, e.FullPath))
        {
            Raise();
        }
    }

    // The buffer overflowed (very many changes at once, e.g. a checkout): changes were lost, so read everything again.
    private void OnError(object sender, ErrorEventArgs e)
    {
        DebugLog.Write("folder watcher", e.GetException());
        Raise();
    }

    // On a thread-pool thread an exception would end the app: it is logged instead.
    private void Raise()
    {
        try
        {
            Changed?.Invoke(this, EventArgs.Empty);
        }
        catch (Exception e)
        {
            DebugLog.Write("folder watcher: Changed handler", e);
        }
    }

    public void Dispose()
    {
        watcher.EnableRaisingEvents = false;
        watcher.Dispose();
    }
}
