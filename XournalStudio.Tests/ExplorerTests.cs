using LibGit2Sharp;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

public sealed class ExplorerTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-explorer-").FullName;

    public void Dispose()
    {
        var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint };
        foreach (var file in Directory.EnumerateFiles(temp, "*", options))
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
    public void TreeShowsEverythingOnDisk()
    {
        Touch("GET/UE/blatt1.pdf");
        Touch("GET/UE/blatt1.xopp");
        Touch("GET/UE/blatt2_kommentiert.pdf");
        Touch("GET/UE/notes.txt");
        Touch("GET/UE/blatt4.xopp.bg.pdf");
        Directory.CreateDirectory(Path.Combine(temp, "Leer"));
        // Only what Windows marks hidden is left out (git marks its .git folder hidden).
        Touch(".git/x.pdf");
        File.SetAttributes(Path.Combine(temp, ".git"), FileAttributes.Directory | FileAttributes.Hidden);
        // A junction back to the project folder is shown but not entered (endless recursion).
        string loop = Path.Combine(temp, "GET", "UE", "Schleife");
        CreateJunction(loop, temp);

        var root = ProjectTree.Read(temp);
        Directory.Delete(loop); // removes the link only

        Assert.Equal(["GET", "Leer"], root.Children.Select(c => c.Name));
        var ue = Assert.Single(root.Children[0].Children);
        Assert.Equal(["Schleife", "blatt1.pdf", "blatt1.xopp", "blatt2_kommentiert.pdf", "blatt4.xopp.bg.pdf", "notes.txt"],
            ue.Children.Select(c => c.Name));
        Assert.Empty(ue.Children[0].Children);
        Assert.True(root.Children[1].IsFolder);
    }

    [Fact]
    public void FolderStatusFollowsTheFilesBelow()
    {
        string root = Path.Combine(temp, "repo");
        var status = new Dictionary<string, GitFileState>
        {
            [Path.Combine(root, "A", "B", "neu.xopp")] = GitFileState.New,
            [Path.Combine(root, "A", "alt.xopp")] = GitFileState.Modified,
            [Path.Combine(root, "C", "neu.pdf")] = GitFileState.New,
            [Path.Combine(root, "top.xopp")] = GitFileState.Modified,
        };

        var all = GitSync.WithFolders(status, root);

        Assert.Equal(GitFileState.Modified, all[Path.Combine(root, "A")]);
        Assert.Equal(GitFileState.New, all[Path.Combine(root, "A", "B")]);
        Assert.Equal(GitFileState.New, all[Path.Combine(root, "c")]); // case-insensitive like the file system
        Assert.False(all.ContainsKey(root));
        Assert.Equal(GitFileState.New, all[Path.Combine(root, "A", "B", "neu.xopp")]);
    }

    [Fact]
    public void DeletedFileMarksItsFolderModified()
    {
        var (a, _) = TwoClones();
        Directory.CreateDirectory(Path.Combine(a, "UE"));
        File.WriteAllText(Path.Combine(a, "UE", "weg.xopp"), "x");
        GitSync.Sync(a, null, "PC-A", Now);
        File.Delete(Path.Combine(a, "UE", "weg.xopp"));
        File.WriteAllText(Path.Combine(a, "UE", "bleibt.xopp"), "x");

        var all = GitSync.WithFolders(GitSync.GetStatus(a), a);

        Assert.Equal(GitFileState.Modified, all[Path.Combine(a, "UE")]);
    }

    [Fact]
    public void NamesAreCheckedLikeWindowsDoes()
    {
        Assert.Null(ProjectFiles.ValidateName("Blatt 3.xopp"));
        Assert.NotNull(ProjectFiles.ValidateName(""));
        Assert.NotNull(ProjectFiles.ValidateName("a/b"));
        Assert.NotNull(ProjectFiles.ValidateName("a:b.xopp"));
        Assert.NotNull(ProjectFiles.ValidateName("Ordner."));
        Assert.NotNull(ProjectFiles.ValidateName("con.xopp"));

        Touch("Neues Dokument.xopp");
        Touch("Neues Dokument (2).xopp");
        Assert.Equal("Neues Dokument (3).xopp", ProjectFiles.UniqueName(temp, "Neues Dokument", ".xopp"));
        Assert.Equal("Neuer Ordner", ProjectFiles.UniqueName(temp, "Neuer Ordner"));
    }

    [Fact]
    public void RenamedPathsAreMapped()
    {
        string ue = Path.Combine(temp, "GET", "UE");
        string renamed = Path.Combine(temp, "GET", "Übung");
        Assert.True(ProjectFiles.IsAtOrBelow(Path.Combine(ue, "a.xopp"), ue));
        Assert.True(ProjectFiles.IsAtOrBelow(ue, ue));
        Assert.False(ProjectFiles.IsAtOrBelow(Path.Combine(temp, "GET", "UE2", "a.xopp"), ue));
        Assert.False(ProjectFiles.IsAtOrBelow(null, ue));
        Assert.Equal(Path.Combine(renamed, "a.xopp"), ProjectFiles.MapRenamed(Path.Combine(ue, "a.xopp"), ue, renamed));
        Assert.Equal(Path.Combine(temp, "b.xopp"), ProjectFiles.MapRenamed(Path.Combine(temp, "b.xopp"), ue, renamed));

        Assert.Equal("blatt1", ProjectFiles.EditableName(Path.Combine(ue, "blatt1.xopp"), false));
        Assert.Equal("blatt2.xopp", ProjectFiles.NewName(Path.Combine(ue, "blatt1.xopp"), false, "blatt2"));
        Assert.Equal("UE", ProjectFiles.EditableName(ue, true));
    }

    [Fact]
    public async Task RenameRenamesOnlyThatFile()
    {
        string doc = Touch("UE/blatt1.xopp", "doc");
        Touch("UE/blatt1.xopp.bg.pdf", "pdf");
        Touch("UE/blatt1.pdf", "lecture");

        string renamed = await ProjectFiles.RenameAsync(doc, false, "Lösung 1.xopp");

        Assert.Equal(Path.Combine(temp, "UE", "Lösung 1.xopp"), renamed);
        Assert.Equal("doc", File.ReadAllText(renamed));
        Assert.True(File.Exists(Path.Combine(temp, "UE", "blatt1.xopp.bg.pdf")));
        Assert.True(File.Exists(Path.Combine(temp, "UE", "blatt1.pdf")));
        Assert.False(File.Exists(doc));
        await Assert.ThrowsAnyAsync<Exception>(() => ProjectFiles.RenameAsync(renamed, false, "blatt1.pdf"));
    }

    [Fact]
    public async Task FoldersAreCreatedAndRenamed()
    {
        string created = await ProjectFiles.CreateFolderAsync(temp, "Übung 1");
        Touch("Übung 1/a.xopp");

        string renamed = await ProjectFiles.RenameAsync(created, true, "Übung 2");

        Assert.Equal(Path.Combine(temp, "Übung 2"), renamed);
        Assert.True(File.Exists(Path.Combine(renamed, "a.xopp")));
        await Assert.ThrowsAnyAsync<Exception>(() => ProjectFiles.CreateFolderAsync(temp, "Übung 2"));
    }

    [Fact]
    public async Task WatcherReportsChangesOutsideHiddenFolders()
    {
        Directory.CreateDirectory(Path.Combine(temp, ".git"));
        Assert.True(FolderWatcher.IsInHiddenFolder(temp, Path.Combine(temp, ".git", "index")));
        Assert.True(FolderWatcher.IsInHiddenFolder(temp, Path.Combine(temp, ".git")));
        Assert.False(FolderWatcher.IsInHiddenFolder(temp, Path.Combine(temp, ".gitignore")));
        Assert.False(FolderWatcher.IsInHiddenFolder(temp, Path.Combine(temp, "UE", "a.xopp")));

        using var watcher = new FolderWatcher(temp);
        var changed = new TaskCompletionSource();
        watcher.Changed += (_, _) => changed.TrySetResult();
        Touch(".git/index");
        await Task.Delay(300);
        Assert.False(changed.Task.IsCompleted);
        Touch("UE/neu.xopp");
        Assert.Same(changed.Task, await Task.WhenAny(changed.Task, Task.Delay(5000)));
    }

    private static void CreateJunction(string link, string target)
    {
        var start = new System.Diagnostics.ProcessStartInfo("cmd.exe", $"/c mklink /J \"{link}\" \"{target}\"")
        {
            CreateNoWindow = true,
            UseShellExecute = false,
            RedirectStandardOutput = true,
        };
        using var process = System.Diagnostics.Process.Start(start)!;
        process.WaitForExit();
        Assert.Equal(0, process.ExitCode);
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
