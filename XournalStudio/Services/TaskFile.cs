using System.Text;

namespace XournalStudio.Services;

/// <summary>
/// A todo.txt file, changed line by line: each change reads the file again right before (so what other apps or a git
/// pull wrote meanwhile stays), touches only its own line and keeps the file's line endings, encoding and final newline.
/// Written to "&lt;file&gt;~" first, then replaced.
/// </summary>
public sealed class TaskFile(string path)
{
    public string Path { get; } = path;

    public bool Exists => File.Exists(Path);

    /// <summary>The lines without line endings (none if the file does not exist).</summary>
    public List<string> ReadLines() => Exists ? Read().Lines : [];

    /// <summary>Replaces the first line equal to <paramref name="oldLine"/>. False: it is no longer there.</summary>
    public bool Replace(string oldLine, string newLine) => Change(lines =>
    {
        int index = lines.IndexOf(oldLine);
        if (index < 0)
        {
            return false;
        }
        lines[index] = newLine;
        return true;
    });

    /// <summary>Removes the first line equal to <paramref name="line"/>. False: it is no longer there.</summary>
    public bool Remove(string line) => Change(lines => lines.Remove(line));

    /// <summary>Adds a line at the end (creates the file if needed).</summary>
    public void Append(string line) => Change(lines =>
    {
        lines.Add(line);
        return true;
    });

    private bool Change(Func<List<string>, bool> change)
    {
        var content = Exists ? Read() : new Content([], "\n", new UTF8Encoding(false), true);
        if (!change(content.Lines))
        {
            return false;
        }
        var text = new StringBuilder();
        for (int i = 0; i < content.Lines.Count; i++)
        {
            text.Append(content.Lines[i]);
            if (i < content.Lines.Count - 1 || content.FinalNewline)
            {
                text.Append(content.NewLine);
            }
        }
        string temp = Path + "~";
        File.WriteAllText(temp, text.ToString(), content.Encoding);
        File.Move(temp, Path, overwrite: true);
        return true;
    }

    /// <summary>
    /// UTF-8 (todo.txt's encoding; with or without byte order mark) or another Unicode encoding with byte order mark,
    /// written back the same way. Anything else (an ANSI file) is refused rather than damaged: <see cref="InvalidDataException"/>.
    /// </summary>
    private Content Read()
    {
        // .NET's byte order mark detection (UTF-8, UTF-16, UTF-32); without one strict UTF-8.
        string text;
        Encoding encoding;
        try
        {
            using var reader = new StreamReader(Path, new UTF8Encoding(false, true), detectEncodingFromByteOrderMarks: true);
            text = reader.ReadToEnd();
            encoding = reader.CurrentEncoding;
        }
        catch (DecoderFallbackException e)
        {
            throw new InvalidDataException($"{Path} ist nicht in UTF-8 gespeichert (todo.txt-Format); unverändert gelassen.", e);
        }
        string newLine = text.Contains("\r\n") ? "\r\n" : "\n";
        bool finalNewline = text.EndsWith('\n');
        var lines = text.Split('\n').Select(l => l.TrimEnd('\r')).ToList();
        if (finalNewline)
        {
            lines.RemoveAt(lines.Count - 1);
        }
        if (text.Length == 0)
        {
            lines.Clear();
            finalNewline = true;
        }
        return new Content(lines, newLine, encoding, finalNewline);
    }

    private sealed record Content(List<string> Lines, string NewLine, Encoding Encoding, bool FinalNewline);
}
