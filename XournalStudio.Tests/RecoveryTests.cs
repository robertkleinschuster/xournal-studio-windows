using System.Diagnostics;
using System.Text.Json;
using XournalStudio.Model;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

public sealed class RecoveryTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-recovery-").FullName;

    public void Dispose() => Directory.Delete(temp, true);

    private string Folder => Path.Combine(temp, "Recovery");

    private static RecoveryInfo Info(string? documentPath, string referenceBase, int processId, DateTime processStart) =>
        new(documentPath, referenceBase, null, new DateTime(2026, 11, 15, 22, 5, 0), DateTime.Now, processId, processStart);

    [Fact]
    public async Task CopyOfAnEndedSessionIsOfferedAndRestoresTheDocument()
    {
        // A document next to its PDF, with unsaved changes (one more page than on disk).
        string docs = Path.Combine(temp, "Uni", "GET");
        Directory.CreateDirectory(docs);
        string pdf = Path.Combine(docs, "Skript.pdf");
        File.WriteAllText(pdf, "");
        var doc = XoppDocument.FromPdf(pdf, [(595, 842), (595, 842)]);
        string path = Path.Combine(docs, "Skript.xopp");
        doc.Save(path);
        doc.Pages.Add(XoppPage.CreateGraph(595, 842));

        var recovery = new Recovery(Folder);
        var info = Info(doc.FilePath, doc.ReferenceBase, 999_999_999, DateTime.Now); // no such process
        await recovery.WriteAsync("a", doc.Snapshot(info.ReferenceBase), info);

        var entry = Assert.Single(recovery.FindAbandoned());
        Assert.Equal(path, entry.Info.DocumentPath);
        var restored = XoppDocument.LoadCopy(entry.CopyPath, entry.Info.DocumentPath, entry.Info.ReferenceBase);
        Assert.Equal(3, restored.Pages.Count);
        Assert.Equal(path, restored.FilePath);
        Assert.Equal(pdf, restored.PdfPath); // the copy lies elsewhere, the reference still finds the PDF

        restored.Save(path);
        Assert.Equal("Skript.pdf", (string?)ReadBackground(path).Attribute("filename")); // still relative

        recovery.Delete("a");
        Assert.Empty(recovery.FindAbandoned());
    }

    [Fact]
    public async Task CopyOfARunningSessionIsNotOffered()
    {
        using var self = Process.GetCurrentProcess();
        var recovery = new Recovery(Folder);
        var doc = XoppDocument.CreateNew();
        var info = Info(null, doc.ReferenceBase, self.Id, Recovery.CurrentProcessStart());
        await recovery.WriteAsync("b", doc.Snapshot(info.ReferenceBase), info);

        Assert.Empty(recovery.FindAbandoned());
        // The same process id after a restart (another start time) is another session.
        File.WriteAllText(Path.Combine(Folder, "b.json"), JsonSerializer.Serialize(info with { ProcessStart = info.ProcessStart.AddHours(-1) }));
        Assert.Single(recovery.FindAbandoned());
    }

    [Fact]
    public async Task NeverSavedDocumentOnAPdfRestoresWithItsPdf()
    {
        string pdf = Path.Combine(temp, "VO", "Folien 3.pdf");
        Directory.CreateDirectory(Path.GetDirectoryName(pdf)!);
        File.WriteAllText(pdf, "");
        var doc = XoppDocument.FromPdf(pdf, [(595, 842)]);
        var recovery = new Recovery(Folder);
        var info = Info(null, doc.ReferenceBase, 999_999_999, DateTime.Now);
        await recovery.WriteAsync("c", doc.Snapshot(info.ReferenceBase), info);

        var entry = Assert.Single(recovery.FindAbandoned());
        var restored = XoppDocument.LoadCopy(entry.CopyPath, null, entry.Info.ReferenceBase);
        Assert.Null(restored.FilePath);
        Assert.Equal(pdf, restored.PdfPath);
        Assert.Equal("Folien 3", restored.SuggestedName);
    }

    [Fact]
    public void DefaultNameIsXournalsDateAndTime()
    {
        Assert.Equal("2026-11-15-Notiz-22-05", XoppDocument.DefaultName(new DateTime(2026, 11, 15, 22, 5, 30)));
        var doc = XoppDocument.CreateNew();
        doc.CreatedAt = new DateTime(2026, 1, 2, 3, 4, 0);
        Assert.Equal("2026-01-02-Notiz-03-04", doc.SuggestedName);
    }

    private static System.Xml.Linq.XElement ReadBackground(string path)
    {
        using var file = File.OpenRead(path);
        using var gzip = new System.IO.Compression.GZipStream(file, System.IO.Compression.CompressionMode.Decompress);
        return System.Xml.Linq.XDocument.Load(gzip).Descendants("background").First();
    }
}
