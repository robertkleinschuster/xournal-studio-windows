using LibGit2Sharp;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

public sealed class ExplorerTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-explorer-").FullName;

    public void Dispose()
    {
        foreach (var file in Directory.EnumerateFiles(temp, "*", SearchOption.AllDirectories))
        {
            File.SetAttributes(file, FileAttributes.Normal); // git objects are read-only
        }
        Directory.Delete(temp, true);
    }

    private string Touch(string relative, string content = "x")
    {
        string path = Path.Combine(temp, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
        return path;
    }

    [Fact]
    public void TreeShowsDocumentsOnly()
    {
        Touch("GET/UE/blatt1.pdf");
        Touch("GET/UE/blatt1.xopp");
        Touch("GET/UE/blatt2.pdf");
        Touch("GET/UE/blatt2_kommentiert.pdf");
        Touch("GET/UE/blatt3.xopp.pages.pdf");
        Touch("GET/UE/blatt3.portals.pdf");
        Touch("GET/UE/notes.txt");
        Touch("Leer/readme.md");
        Touch(".git/x.pdf");

        var root = ProjectTree.Read(temp);

        var get = Assert.Single(root.Children);
        Assert.Equal("GET", get.Name);
        var ue = Assert.Single(get.Children);
        Assert.Equal(["blatt1.xopp", "blatt2.pdf"], ue.Children.Select(c => c.Name));
    }

    private string Clone(string remote, string name)
    {
        string path = Repository.Clone(remote, Path.Combine(temp, name));
        using var repo = new Repository(path);
        repo.Config.Set("user.name", "Test", ConfigurationLevel.Local);
        repo.Config.Set("user.email", "test@example.com", ConfigurationLevel.Local);
        return Path.TrimEndingDirectorySeparator(repo.Info.WorkingDirectory);
    }

    private (string A, string B) TwoClones()
    {
        string remote = Repository.Init(Path.Combine(temp, "remote.git"), isBare: true);
        string a = Clone(remote, "a");
        File.WriteAllText(Path.Combine(a, "start.xopp"), "start");
        using (var repo = new Repository(a))
        {
            Commands.Stage(repo, "*");
            var sig = new Signature("Test", "test@example.com", DateTimeOffset.Now);
            repo.Commit("start", sig, sig);
            string head = repo.Head.CanonicalName; // master or init.defaultBranch
            repo.Network.Push(repo.Network.Remotes["origin"], $"{head}:{head}");
            repo.Branches.Update(repo.Head, b => b.Remote = "origin", b => b.UpstreamBranch = head);
        }
        return (a, Clone(remote, "b"));
    }

    private static readonly DateTimeOffset Now = new(2026, 11, 15, 16, 30, 0, TimeSpan.Zero);

    [Fact]
    public void StatusReportsNewAndModified()
    {
        var (a, _) = TwoClones();
        File.WriteAllText(Path.Combine(a, "start.xopp"), "changed");
        File.WriteAllText(Path.Combine(a, "neu.xopp"), "new");

        var status = GitSync.GetStatus(a);

        Assert.Equal(GitFileState.Modified, status[Path.Combine(a, "start.xopp")]);
        Assert.Equal(GitFileState.New, status[Path.Combine(a, "neu.xopp")]);
        Assert.Equal(a, GitSync.FindRoot(Path.Combine(a, "start.xopp")));
    }

    [Fact]
    public void SyncCommitsPushesAndPulls()
    {
        var (a, b) = TwoClones();
        File.WriteAllText(Path.Combine(a, "start.xopp"), "from a");

        var pushedFromA = GitSync.Sync(a, null, "PC-A", Now);
        var pulledToB = GitSync.Sync(b, null, "PC-B", Now);

        Assert.True(pushedFromA.Committed);
        Assert.True(pushedFromA.Pushed);
        Assert.False(pulledToB.Committed);
        Assert.Equal([Path.Combine(b, "start.xopp")], pulledToB.PulledFiles);
        Assert.Equal("from a", File.ReadAllText(Path.Combine(b, "start.xopp")));
        using var repo = new Repository(b);
        Assert.Equal("Sync from PC-A (2026-11-15 16:30)", repo.Head.Tip.MessageShort);
    }

    [Fact]
    public void SyncRebasesOwnCommitsOntoRemote()
    {
        var (a, b) = TwoClones();
        File.WriteAllText(Path.Combine(a, "a.xopp"), "a");
        File.WriteAllText(Path.Combine(b, "b.xopp"), "b");

        GitSync.Sync(a, null, "PC-A", Now);
        var result = GitSync.Sync(b, null, "PC-B", Now);

        Assert.True(result.Pushed);
        Assert.Equal([Path.Combine(b, "a.xopp")], result.PulledFiles);
        using var repo = new Repository(b);
        Assert.Equal("Sync from PC-B (2026-11-15 16:30)", repo.Head.Tip.MessageShort);
        Assert.Single(repo.Head.Tip.Parents); // rebased, no merge commit
        Assert.Equal(0, repo.Head.TrackingDetails.AheadBy);
    }

    [Fact]
    public void ConflictIsAbortedAndReported()
    {
        var (a, b) = TwoClones();
        File.WriteAllText(Path.Combine(a, "start.xopp"), "a");
        File.WriteAllText(Path.Combine(b, "start.xopp"), "b");
        GitSync.Sync(a, null, "PC-A", Now);

        var e = Assert.Throws<GitSyncConflictException>(() => GitSync.Sync(b, null, "PC-B", Now));

        Assert.Contains("start.xopp", e.Message);
        Assert.Equal("b", File.ReadAllText(Path.Combine(b, "start.xopp")));
        using var repo = new Repository(b);
        Assert.Equal(CurrentOperation.None, repo.Info.CurrentOperation);
    }
}
