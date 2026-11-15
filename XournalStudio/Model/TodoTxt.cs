using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace XournalStudio.Model;

/// <summary>
/// One line of a todo.txt file (https://github.com/todotxt/todo.txt), e.g.
/// "x 2026-11-05 2026-11-01 (A) GET HÜ Abgabe +GET due:2026-11-05 time:18:00". The description is kept verbatim
/// (tags of other apps included), so a task is written back as other apps wrote it.
/// </summary>
public sealed record TodoTask(bool Done, char? Priority, DateOnly? Completed, DateOnly? Created, string Description)
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    private static readonly Regex TagPattern = new(@"(?<=^|\s)([^\s:]+):(\S+)", RegexOptions.Compiled);

    /// <summary>The value of a key:value tag (the first one), if present.</summary>
    public string? Tag(string key) =>
        TagPattern.Matches(Description).FirstOrDefault(m => m.Groups[1].Value == key && !IsUrl(m))?.Groups[2].Value;

    /// <summary>due:YYYY-MM-DD.</summary>
    public DateOnly? Due => TodoTxt.ParseDate(Tag("due"));

    /// <summary>time:HH:MM, the due time (kept apart from due: so other apps still understand the date).</summary>
    public TimeOnly? Time =>
        TimeOnly.TryParseExact(Tag("time") ?? "", "H:mm", Inv, DateTimeStyles.None, out var t) ? t : null;

    /// <summary>The description without key:value tags (links stay), for showing.</summary>
    public string Title => Regex.Replace(TagPattern.Replace(Description, m => IsUrl(m) ? m.Value : ""), @"\s{2,}", " ").Trim();

    // "https://..." looks like a tag but is not one.
    private static bool IsUrl(Match m) => m.Groups[2].Value.StartsWith("//", StringComparison.Ordinal);

    /// <summary>Done today. The priority goes into a pri: tag, as todo.txt apps do (a done task has none).</summary>
    public TodoTask Complete(DateOnly today) => Done ? this : this with
    {
        Done = true,
        Completed = today,
        Priority = null,
        Description = Priority is char p ? TodoTxt.SetTag(Description, "pri", p.ToString()) : Description,
    };

    /// <summary>Open again (the priority back from pri:).</summary>
    public TodoTask Reopen()
    {
        if (!Done)
        {
            return this;
        }
        string? pri = Tag("pri");
        bool hasPriority = pri is { Length: 1 } && pri[0] is >= 'A' and <= 'Z';
        return this with
        {
            Done = false,
            Completed = null,
            Priority = hasPriority ? pri![0] : Priority,
            Description = hasPriority ? TodoTxt.SetTag(Description, "pri", null) : Description,
        };
    }

    public override string ToString() => TodoTxt.Format(this);
}

/// <summary>Reading and writing todo.txt lines.</summary>
public static class TodoTxt
{
    private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

    /// <summary>Parses a line; null for blank lines.</summary>
    public static TodoTask? Parse(string line)
    {
        string rest = line.Trim();
        if (rest.Length == 0)
        {
            return null;
        }
        bool done = false;
        char? priority = null;
        DateOnly? completed = null, created = null;
        if (rest.StartsWith("x ", StringComparison.Ordinal))
        {
            done = true;
            rest = rest[2..].TrimStart();
            if (TakeDate(ref rest) is { } c)
            {
                completed = c;
                created = TakeDate(ref rest);
            }
        }
        else
        {
            if (rest.Length >= 4 && rest[0] == '(' && rest[1] is >= 'A' and <= 'Z' && rest[2] == ')' && rest[3] == ' ')
            {
                priority = rest[1];
                rest = rest[4..].TrimStart();
            }
            created = TakeDate(ref rest);
        }
        return new TodoTask(done, priority, completed, created, rest);
    }

    public static string Format(TodoTask task)
    {
        var line = new StringBuilder();
        if (task.Done)
        {
            line.Append("x ");
            if (task.Completed is { } c)
            {
                line.Append(FormatDate(c)).Append(' ');
            }
        }
        else if (task.Priority is char p)
        {
            line.Append('(').Append(p).Append(") ");
        }
        if (task.Created is { } created && (!task.Done || task.Completed != null))
        {
            line.Append(FormatDate(created)).Append(' ');
        }
        return line.Append(task.Description).ToString();
    }

    /// <summary>A new task created today, with an optional due date.</summary>
    public static TodoTask Create(string text, DateOnly today, DateOnly? due)
    {
        string description = text.Trim();
        if (due is { } d)
        {
            description = SetTag(description, "due", FormatDate(d));
        }
        return new TodoTask(false, null, null, today, description);
    }

    /// <summary>Sets (or with null removes) a key:value tag; an existing one keeps its place.</summary>
    public static string SetTag(string description, string key, string? value)
    {
        var pattern = new Regex(@"(?<=^|\s)" + Regex.Escape(key) + @":\S+");
        if (pattern.IsMatch(description))
        {
            string replaced = value != null
                ? pattern.Replace(description, $"{key}:{value}", 1)
                : pattern.Replace(description, "", 1);
            return Regex.Replace(replaced, @"\s{2,}", " ").Trim();
        }
        return value != null ? $"{description} {key}:{value}".Trim() : description;
    }

    public static DateOnly? ParseDate(string? text) =>
        DateOnly.TryParseExact(text ?? "", "yyyy-MM-dd", Inv, DateTimeStyles.None, out var d) ? d : null;

    public static string FormatDate(DateOnly date) => date.ToString("yyyy-MM-dd", Inv);

    /// <summary>
    /// The value of a doc: tag linking a document page, relative to the task file's folder with '/' (spaces, % and #
    /// encoded, they would end the tag or the path), and the 1-based page as "#pN" – as the GTK Xournal Studio wrote.
    /// </summary>
    public static string DocumentLink(string taskFile, string document, int pageIndex)
    {
        string relative = Path.GetRelativePath(Path.GetDirectoryName(Path.GetFullPath(taskFile))!, Path.GetFullPath(document))
            .Replace('\\', '/');
        var link = new StringBuilder();
        foreach (char c in relative)
        {
            link.Append(c switch { ' ' => "%20", '%' => "%25", '#' => "%23", _ => c.ToString() });
        }
        return pageIndex >= 0 ? $"{link}#p{pageIndex + 1}" : link.ToString();
    }

    /// <summary>The document and 0-based page (-1: none) of a doc: value of a task in <paramref name="taskFile"/>.</summary>
    public static (string Document, int PageIndex)? ResolveDocumentLink(string taskFile, string value)
    {
        int page = -1;
        int hash = value.LastIndexOf("#p", StringComparison.Ordinal);
        if (hash >= 0)
        {
            if (!int.TryParse(value[(hash + 2)..], NumberStyles.None, Inv, out int n) || n < 1)
            {
                return null;
            }
            page = n - 1;
            value = value[..hash];
        }
        string path = Uri.UnescapeDataString(value).Replace('/', Path.DirectorySeparatorChar);
        if (path.Length == 0)
        {
            return null;
        }
        return (Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Path.GetFullPath(taskFile))!, path)), page);
    }

    private static DateOnly? TakeDate(ref string rest)
    {
        if (rest.Length >= 10 && ParseDate(rest[..10]) is { } date && (rest.Length == 10 || rest[10] == ' '))
        {
            rest = rest[10..].TrimStart();
            return date;
        }
        return null;
    }
}
