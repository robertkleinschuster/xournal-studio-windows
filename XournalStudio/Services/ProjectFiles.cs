using Windows.Storage;

namespace XournalStudio.Services;

/// <summary>
/// File operations of the explorer with the Windows storage API (StorageFile/StorageFolder): new document, new
/// folder, rename, delete to the recycle bin. A .xopp is renamed and deleted together with its upstream companion
/// file – the attached PDF background "&lt;name&gt;.xopp.bg.pdf" (domain "attach" in upstream SaveHandler.cpp) –
/// because Xournal++ finds that one by the document's name.
/// </summary>
public static class ProjectFiles
{
    public const string DocumentExtension = ".xopp";

    /// <summary>Upstream's attached PDF of a document: the name without extensions + ".xopp.bg.pdf".</summary>
    public const string AttachedPdfSuffix = ".xopp.bg.pdf";

    private static readonly string[] ReservedNames =
    [
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    ];

    /// <summary>Why <paramref name="name"/> cannot be a file or folder name (German, for the dialog), or null.</summary>
    public static string? ValidateName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            return "Der Name darf nicht leer sein.";
        }
        if (name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            return "Ein Name darf keines dieser Zeichen enthalten: \\ / : * ? \" < > |";
        }
        if (name.EndsWith('.') || name.EndsWith(' '))
        {
            return "Ein Name darf nicht mit einem Punkt oder Leerzeichen enden.";
        }
        string stem = name.Split('.')[0].TrimEnd();
        if (ReservedNames.Contains(stem, StringComparer.OrdinalIgnoreCase))
        {
            return $"„{stem}“ ist unter Windows als Name reserviert.";
        }
        return null;
    }

    /// <summary>"Name", or "Name (2)", "Name (3)", ... like File Explorer, so that nothing in the folder is overwritten.</summary>
    public static string UniqueName(string folder, string name, string extension = "")
    {
        for (int i = 1; ; i++)
        {
            string candidate = (i == 1 ? name : $"{name} ({i})") + extension;
            if (!Path.Exists(Path.Combine(folder, candidate)))
            {
                return candidate;
            }
        }
    }

    /// <summary>True when <paramref name="path"/> is <paramref name="item"/> or lies in the folder <paramref name="item"/>.</summary>
    public static bool IsAtOrBelow(string? path, string item)
    {
        if (path == null)
        {
            return false;
        }
        string full = Path.GetFullPath(path), at = Path.TrimEndingDirectorySeparator(Path.GetFullPath(item));
        return string.Equals(full, at, StringComparison.OrdinalIgnoreCase) ||
            full.StartsWith(at + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Where <paramref name="path"/> is after <paramref name="item"/> was renamed to <paramref name="renamed"/>.</summary>
    public static string MapRenamed(string path, string item, string renamed)
    {
        if (!IsAtOrBelow(path, item))
        {
            return path;
        }
        string full = Path.GetFullPath(path), at = Path.TrimEndingDirectorySeparator(Path.GetFullPath(item));
        return Path.TrimEndingDirectorySeparator(Path.GetFullPath(renamed)) + full[at.Length..];
    }

    /// <summary>The upstream companion files of <paramref name="path"/> that exist (the attached PDF of a .xopp).</summary>
    public static IReadOnlyList<string> Companions(string path)
    {
        if (!path.EndsWith(DocumentExtension, StringComparison.OrdinalIgnoreCase))
        {
            return [];
        }
        string attached = path[..^DocumentExtension.Length] + AttachedPdfSuffix;
        return File.Exists(attached) ? [attached] : [];
    }

    /// <summary>
    /// The name an item gets: a file keeps its extension (only the name before it is edited, as the dialog shows it).
    /// </summary>
    public static string NewName(string path, bool isFolder, string editedName) =>
        isFolder ? editedName : editedName + Path.GetExtension(path);

    /// <summary>The part of the name the rename dialog shows: a file without its extension.</summary>
    public static string EditableName(string path, bool isFolder) =>
        isFolder ? Path.GetFileName(Path.TrimEndingDirectorySeparator(path)) : Path.GetFileNameWithoutExtension(path);

    /// <summary>Renames a file (with its companions) or folder; fails if the name is taken. Returns the new path.</summary>
    public static async Task<string> RenameAsync(string path, bool isFolder, string newName)
    {
        if (isFolder)
        {
            var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetFullPath(path));
            await folder.RenameAsync(newName, NameCollisionOption.FailIfExists);
            return folder.Path;
        }
        var companions = Companions(path);
        var file = await StorageFile.GetFileFromPathAsync(Path.GetFullPath(path));
        await file.RenameAsync(newName, NameCollisionOption.FailIfExists);
        string stem = Path.GetFileNameWithoutExtension(newName);
        foreach (string companion in companions)
        {
            var companionFile = await StorageFile.GetFileFromPathAsync(Path.GetFullPath(companion));
            await companionFile.RenameAsync(stem + AttachedPdfSuffix, NameCollisionOption.FailIfExists);
        }
        return file.Path;
    }

    /// <summary>Moves a file (with its companions) or folder to the recycle bin (<see cref="StorageDeleteOption.Default"/>).</summary>
    public static async Task DeleteAsync(string path, bool isFolder)
    {
        if (isFolder)
        {
            var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetFullPath(path));
            await folder.DeleteAsync(StorageDeleteOption.Default);
            return;
        }
        var companions = Companions(path);
        await (await StorageFile.GetFileFromPathAsync(Path.GetFullPath(path))).DeleteAsync(StorageDeleteOption.Default);
        foreach (string companion in companions)
        {
            await (await StorageFile.GetFileFromPathAsync(Path.GetFullPath(companion))).DeleteAsync(StorageDeleteOption.Default);
        }
    }

    /// <summary>Creates a folder; fails if the name is taken. Returns its path.</summary>
    public static async Task<string> CreateFolderAsync(string parent, string name)
    {
        var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetFullPath(parent));
        return (await folder.CreateFolderAsync(name, CreationCollisionOption.FailIfExists)).Path;
    }
}
