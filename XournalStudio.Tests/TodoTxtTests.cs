using XournalStudio.Model;
using XournalStudio.Services;
using Xunit;

namespace XournalStudio.Tests;

public sealed class TodoTxtTests : IDisposable
{
    private readonly string temp = Directory.CreateTempSubdirectory("xs-todo-").FullName;

    public void Dispose() => Directory.Delete(temp, true);

    private static readonly DateOnly Today = new(2026, 11, 15);

    [Theory]
    [InlineData("x 2026-11-05 2026-11-01 GET HÜ Abgabe due:2026-11-05 time:18:00")]
    [InlineData("2026-11-04 Knotenspannungsverfahren Klassisch (Teilklausur)")]
    [InlineData("x 2026-11-01 Vorbereitung Mathe A VO bzgl. Funktionen  due:2026-10-28")]
    [InlineData("(A) 2026-11-06 Polynomdivision +Mathe @uni")]
    [InlineData("Ohne Datum")]
    public void LinesAreWrittenBackAsTheyWere(string line) => Assert.Equal(line, TodoTxt.Parse(line)!.ToString());

    [Fact]
    public void ReadsDatesTagsAndTitle()
    {
        var task = TodoTxt.Parse("x 2026-11-05 2026-11-01 GET HÜ Abgabe due:2026-11-05 time:18:00 siehe https://x.at/a")!;
        Assert.True(task.Done);
        Assert.Equal(new DateOnly(2026, 11, 5), task.Completed);
        Assert.Equal(new DateOnly(2026, 11, 1), task.Created);
        Assert.Equal(new DateOnly(2026, 11, 5), task.Due);
        Assert.Equal(new TimeOnly(18, 0), task.Time);
        Assert.Equal("GET HÜ Abgabe siehe https://x.at/a", task.Title);
        Assert.Null(TodoTxt.Parse("   "));
    }

    [Fact]
    public void CompletingMovesThePriorityIntoATagAndReopeningBringsItBack()
    {
        var task = TodoTxt.Parse("(B) 2026-11-06 Polynomdivision due:2026-11-20")!;
        var done = task.Complete(Today);
        Assert.Equal("x 2026-11-15 2026-11-06 Polynomdivision due:2026-11-20 pri:B", done.ToString());
        Assert.Equal("(B) 2026-11-06 Polynomdivision due:2026-11-20", done.Reopen().ToString());
    }

    [Fact]
    public void NewTaskHasTodaysDateAndItsDueDate()
    {
        Assert.Equal("2026-11-15 GET Altklausur rechnen due:2026-11-20",
            TodoTxt.Create(" GET Altklausur rechnen ", Today, new DateOnly(2026, 11, 20)).ToString());
        Assert.Equal("a due:2026-12-01 b", TodoTxt.SetTag("a due:2026-11-01 b", "due", "2026-12-01"));
        Assert.Equal("a b", TodoTxt.SetTag("a due:2026-11-01 b", "due", null));
    }

    [Fact]
    public void DocumentLinksAreRelativeEncodedAndResolve()
    {
        string todo = Path.Combine(temp, "todo.txt");
        string doc = Path.Combine(temp, "GET", "UE 3", "Blatt#1.xopp");
        string link = TodoTxt.DocumentLink(todo, doc, 2);
        Assert.Equal("GET/UE%203/Blatt%231.xopp#p3", link);
        Assert.Equal((doc, 2), TodoTxt.ResolveDocumentLink(todo, link));
        Assert.Equal((doc, -1), TodoTxt.ResolveDocumentLink(todo, "GET/UE%203/Blatt%231.xopp"));
    }

    [Fact]
    public void FileChangesTouchOnlyTheirLineAndKeepTheFormat()
    {
        string path = Path.Combine(temp, "todo.txt");
        File.WriteAllText(path, "a\r\nb\r\nc"); // CRLF, no final newline
        var file = new TaskFile(path);

        Assert.True(file.Replace("b", "x 2026-11-15 b"));
        Assert.False(file.Replace("b", "never")); // already changed (e.g. by another app)
        file.Append("d");
        Assert.True(file.Remove("a"));
        Assert.Equal("x 2026-11-15 b\r\nc\r\nd", File.ReadAllText(path));

        string fresh = Path.Combine(temp, "neu", "todo.txt");
        Directory.CreateDirectory(Path.GetDirectoryName(fresh)!);
        new TaskFile(fresh).Append("2026-11-15 erste");
        Assert.Equal("2026-11-15 erste\n", File.ReadAllText(fresh));
        Assert.Equal(["2026-11-15 erste"], new TaskFile(fresh).ReadLines());
    }

    [Fact]
    public void ByteOrderMarkStays()
    {
        string path = Path.Combine(temp, "bom.txt");
        File.WriteAllText(path, "a\n", new System.Text.UTF8Encoding(true));
        new TaskFile(path).Replace("a", "ä");
        byte[] bytes = File.ReadAllBytes(path);
        Assert.Equal([0xEF, 0xBB, 0xBF], bytes[..3]);
        Assert.Equal("ä\n", new System.Text.UTF8Encoding(false).GetString(bytes, 3, bytes.Length - 3));
    }
}
