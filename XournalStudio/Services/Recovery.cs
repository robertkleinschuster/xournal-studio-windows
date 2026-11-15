using System.Diagnostics;
using System.Text.Json;
using System.Xml.Linq;
using XournalStudio.Model;

namespace XournalStudio.Services;

/// <summary>What a recovery copy belongs to.</summary>
/// <param name="DocumentPath">The document's file, null if it was never saved.</param>
/// <param name="ReferenceBase">The path PDF references in the copy are relative to (see <see cref="XoppDocument.Snapshot"/>).</param>
/// <param name="PdfPath">The background PDF, for the name of a document that was never saved.</param>
public sealed record RecoveryInfo(
    string? DocumentPath, string ReferenceBase, string? PdfPath, DateTime? CreatedAt, DateTime SavedAt,
    int ProcessId, DateTime ProcessStart);

/// <summary>A recovery copy left behind by a session that ended without the user deciding to save or discard.</summary>
public sealed record RecoveryEntry(string Id, RecoveryInfo Info, string CopyPath);

/// <summary>
/// Crash recovery like Xournal++'s autosave: while a document has unsaved changes, a copy of it is written to the
/// app's own folder (never next to the document: the project folder is git-synced). The copy is removed once the user
/// saved or discarded the changes; a copy whose session is gone (crash, power loss, Windows ended the app) is offered
/// for restoring at the next start.
/// </summary>
public sealed class Recovery
{
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    private readonly string folder;

    public Recovery(string folder)
    {
        this.folder = folder;
    }

    public static Recovery ForApp() => new(Path.Combine(AppSettings.Folder, "Recovery"));

    public static string NewId() => Guid.NewGuid().ToString("N");

    private string CopyPath(string id) => Path.Combine(folder, id + ".xopp");

    private string InfoPath(string id) => Path.Combine(folder, id + ".json");

    /// <summary>
    /// Writes the copy, then its info (a copy without info is never offered: a session that died while writing the
    /// first copy has nothing complete). Runs off the UI thread: <paramref name="snapshot"/> is detached.
    /// </summary>
    public Task WriteAsync(string id, XDocument snapshot, RecoveryInfo info) => Task.Run(() =>
    {
        Directory.CreateDirectory(folder);
        XoppDocument.Write(snapshot, CopyPath(id));
        string temp = InfoPath(id) + "~";
        File.WriteAllText(temp, JsonSerializer.Serialize(info, Json));
        File.Move(temp, InfoPath(id), overwrite: true);
    });

    /// <summary>Removes a session's copy (the user saved or discarded the changes).</summary>
    public void Delete(string id)
    {
        if (!Directory.Exists(folder))
        {
            return; // nothing was ever written (File.Delete throws for a missing folder)
        }
        File.Delete(InfoPath(id));
        File.Delete(CopyPath(id));
        File.Delete(CopyPath(id) + "~");
    }

    /// <summary>
    /// The user discarded a copy at the start: it goes to the recycle bin under a readable name (nothing is lost for
    /// good by one click).
    /// </summary>
    public async Task RecycleAsync(RecoveryEntry entry, string name)
    {
        var file = await Windows.Storage.StorageFile.GetFileFromPathAsync(entry.CopyPath);
        string readable = string.Concat($"{name} (Wiederherstellung {entry.Info.SavedAt:yyyy-MM-dd HH-mm}).xopp"
            .Select(c => Path.GetInvalidFileNameChars().Contains(c) ? '_' : c));
        await file.RenameAsync(readable, Windows.Storage.NameCollisionOption.GenerateUniqueName);
        try
        {
            await file.DeleteAsync(Windows.Storage.StorageDeleteOption.Default);
        }
        catch
        {
            // Not recycled: back to the name its info refers to, so it is offered again (the caller reports the error).
            await file.RenameAsync(Path.GetFileName(entry.CopyPath));
            throw;
        }
        File.Delete(InfoPath(entry.Id));
    }

    /// <summary>Copies whose session is no longer running, newest first.</summary>
    public List<RecoveryEntry> FindAbandoned()
    {
        var entries = new List<RecoveryEntry>();
        if (!Directory.Exists(folder))
        {
            return entries;
        }
        foreach (string infoPath in Directory.EnumerateFiles(folder, "*.json"))
        {
            string id = Path.GetFileNameWithoutExtension(infoPath);
            RecoveryInfo? info;
            try
            {
                info = JsonSerializer.Deserialize<RecoveryInfo>(File.ReadAllText(infoPath));
            }
            catch (Exception e) when (e is JsonException or IOException or UnauthorizedAccessException)
            {
                DebugLog.Write($"recovery info {infoPath} unreadable (not offered, kept)", e);
                continue;
            }
            if (info != null && File.Exists(CopyPath(id)) && !IsRunning(info))
            {
                entries.Add(new RecoveryEntry(id, info, CopyPath(id)));
            }
        }
        return entries.OrderByDescending(e => e.Info.SavedAt).ToList();
    }

    /// <summary>The info file says which process wrote it; it is still running when that process exists with the same start time.</summary>
    private static bool IsRunning(RecoveryInfo info)
    {
        Process process;
        try
        {
            process = Process.GetProcessById(info.ProcessId);
        }
        catch (ArgumentException e)
        {
            // Documented: no process with this id is running.
            DebugLog.Write($"recovery: process {info.ProcessId} not running (session ended)", e);
            return false;
        }
        using (process)
        {
            try
            {
                return process.StartTime == info.ProcessStart;
            }
            catch (Exception e) when (e is InvalidOperationException or System.ComponentModel.Win32Exception)
            {
                // Exited meanwhile, or not ours to inspect (then it is not this app's session either).
                DebugLog.Write($"recovery: process {info.ProcessId} not inspectable (treated as ended)", e);
                return false;
            }
        }
    }

    /// <summary>The process start time recorded in the info, as <see cref="IsRunning"/> compares it.</summary>
    public static DateTime CurrentProcessStart()
    {
        using var self = Process.GetCurrentProcess();
        return self.StartTime;
    }
}
