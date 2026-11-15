using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Text;
using XournalStudio.Model;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The tasks of the project folder's todo.txt: open ones by due date (overdue first, in red), a new task with an
/// optional due date and a link to the page in view (doc: tag, as the GTK Xournal Studio wrote), check to complete,
/// edit or delete from the context menu. Every change rewrites only its own line (<see cref="TaskFile"/>).
/// </summary>
public sealed partial class TasksView : UserControl
{
    private TaskFile? file;
    private Dialogs? dialogs;
    private Func<(string? Document, int PageIndex)> currentPage = () => (null, -1);

    public TasksView()
    {
        InitializeComponent();
    }

    /// <summary>A task's linked page should be opened: the document and its 0-based page (-1: none).</summary>
    public event EventHandler<(string Document, int PageIndex)>? OpenRequested;

    public event EventHandler<(string Title, string Message)>? Failed;

    /// <summary>The user switched done tasks on or off (a setting).</summary>
    public event EventHandler? ShowsDoneChanged;

    /// <param name="page">The open document (null if never saved) and the page in view, for links.</param>
    public void Initialize(Dialogs dialogs, Func<(string? Document, int PageIndex)> page)
    {
        this.dialogs = dialogs;
        currentPage = page;
    }

    public bool ShowsDone
    {
        get => ShowDone.IsChecked == true;
        set => ShowDone.IsChecked = value;
    }

    /// <summary>Shows the todo.txt of <paramref name="folder"/> (again, after changes on disk).</summary>
    public void Show(string? folder)
    {
        file = folder != null ? new TaskFile(Path.Combine(folder, "todo.txt")) : null;
        Reload();
    }

    private void Reload()
    {
        var items = new List<TaskItem>();
        List.ItemsSource = items;
        bool exists = file?.Exists == true;
        NoFile.Visibility = exists ? Visibility.Collapsed : Visibility.Visible;
        AddPanel.Visibility = exists ? Visibility.Visible : Visibility.Collapsed;
        CreateButton.Visibility = file != null ? Visibility.Visible : Visibility.Collapsed;
        NoFileText.Text = file == null
            ? "Kein Ordner geöffnet (Datei → Ordner öffnen)."
            : "Im Projektordner gibt es keine todo.txt.";
        if (!exists)
        {
            FileName.Text = "Aufgaben";
            return;
        }
        List<string> lines;
        try
        {
            lines = file!.ReadLines();
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            DebugLog.Write($"read {file!.Path}", e);
            Failed?.Invoke(this, ("todo.txt konnte nicht gelesen werden", DebugLog.Describe(e)));
            return;
        }
        var today = DateOnly.FromDateTime(DateTime.Now);
        var tasks = lines.Select(l => (Line: l, Task: TodoTxt.Parse(l))).Where(t => t.Task != null).ToList();
        var open = tasks.Where(t => !t.Task!.Done)
            .OrderBy(t => t.Task!.Due ?? DateOnly.MaxValue).ThenBy(t => t.Task!.Time ?? TimeOnly.MaxValue);
        foreach (var (line, task) in open)
        {
            items.Add(new TaskItem(line, task!, today, file.Path));
        }
        if (ShowsDone)
        {
            foreach (var (line, task) in tasks.Where(t => t.Task!.Done).OrderByDescending(t => t.Task!.Completed))
            {
                items.Add(new TaskItem(line, task!, today, file.Path));
            }
        }
        List.ItemsSource = items;
        int count = tasks.Count(t => !t.Task!.Done);
        DebugLog.Write($"tasks: {count} open, {items.Count} shown");
        FileName.Text = count == 1 ? "todo.txt – 1 offen" : $"todo.txt – {count} offen";
        LinkPage.IsEnabled = currentPage().Document != null;
    }

    // ---------------------------------------------------------------- changes

    private void Change(string what, Func<TaskFile, bool> change)
    {
        if (file == null)
        {
            return;
        }
        try
        {
            if (!change(file))
            {
                Failed?.Invoke(this, ("Aufgabe wurde inzwischen geändert",
                    "Eine andere App oder die Synchronisierung hat die Zeile geändert; die Liste ist neu geladen."));
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            DebugLog.Write($"{what} {file.Path}", e);
            Failed?.Invoke(this, ("todo.txt konnte nicht geschrieben werden", DebugLog.Describe(e)));
        }
        Reload();
    }

    private void OnAdd(object sender, RoutedEventArgs e) => Add();

    private void OnNewTaskKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Enter)
        {
            e.Handled = true;
            Add();
        }
    }

    private void Add()
    {
        if (file == null || string.IsNullOrWhiteSpace(NewTask.Text))
        {
            return;
        }
        var today = DateOnly.FromDateTime(DateTime.Now);
        DateOnly? due = NewDue.Date is { } d ? DateOnly.FromDateTime(d.Date) : null;
        var task = TodoTxt.Create(NewTask.Text, today, due);
        var (document, page) = currentPage();
        if (LinkPage.IsChecked == true && document != null)
        {
            task = task with { Description = TodoTxt.SetTag(task.Description, "doc", TodoTxt.DocumentLink(file.Path, document, page)) };
        }
        Change("add task", f =>
        {
            f.Append(task.ToString());
            return true;
        });
        NewTask.Text = "";
        NewDue.Date = null;
        LinkPage.IsChecked = false;
    }

    private void OnDoneClick(object sender, RoutedEventArgs e)
    {
        var item = (TaskItem)((FrameworkElement)sender).DataContext;
        var today = DateOnly.FromDateTime(DateTime.Now);
        var changed = item.Task.Done ? item.Task.Reopen() : item.Task.Complete(today);
        Change("complete task", f => f.Replace(item.Line, changed.ToString()));
    }

    // The row the context menu was opened on (one menu is shared by all rows).
    private TaskItem MenuItemTarget() => (TaskItem)((FrameworkElement)((MenuFlyout)Resources["TaskMenu"]).Target).DataContext;

    private async void OnEdit(object sender, RoutedEventArgs e)
    {
        var item = MenuItemTarget();
        if (dialogs == null)
        {
            return;
        }
        // The text as written (with +projects, @contexts, other tags), the due date with a date picker.
        var text = new TextBox { Header = "Aufgabe", Text = TodoTxt.SetTag(item.Task.Description, "due", null), TextWrapping = TextWrapping.Wrap };
        var due = new CalendarDatePicker { Header = "Fällig am", FirstDayOfWeek = Windows.Globalization.DayOfWeek.Monday };
        if (item.Task.Due is { } d)
        {
            due.Date = d.ToDateTime(TimeOnly.MinValue);
        }
        var dialog = new ContentDialog
        {
            Title = "Aufgabe bearbeiten",
            Content = new StackPanel { Spacing = 12, Children = { text, due } },
            PrimaryButtonText = "Speichern",
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        };
        if (await dialogs.ShowAsync(dialog) != ContentDialogResult.Primary || string.IsNullOrWhiteSpace(text.Text))
        {
            return;
        }
        string description = TodoTxt.SetTag(text.Text.Trim(), "due",
            due.Date is { } date ? TodoTxt.FormatDate(DateOnly.FromDateTime(date.Date)) : null);
        Change("edit task", f => f.Replace(item.Line, (item.Task with { Description = description }).ToString()));
    }

    private async void OnDelete(object sender, RoutedEventArgs e)
    {
        var item = MenuItemTarget();
        if (dialogs != null && await dialogs.ConfirmAsync("Aufgabe löschen?",
                $"„{item.Title}“ wird aus der todo.txt entfernt.", "Löschen"))
        {
            Change("delete task", f => f.Remove(item.Line));
        }
    }

    private void OnCreate(object sender, RoutedEventArgs e) => Change("create todo.txt", f =>
    {
        using (new FileStream(f.Path, FileMode.CreateNew)) { } // never over an existing one
        return true;
    });

    private void OnShowDoneClick(object sender, RoutedEventArgs e)
    {
        Reload();
        ShowsDoneChanged?.Invoke(this, EventArgs.Empty);
    }

    private void OnOpenLink(object sender, RoutedEventArgs e)
    {
        var item = (TaskItem)((FrameworkElement)sender).DataContext;
        if (item.Link is { } link)
        {
            OpenRequested?.Invoke(this, link);
        }
    }
}

/// <summary>A task as the list shows it.</summary>
public sealed class TaskItem
{
    public TaskItem(string line, TodoTask task, DateOnly today, string taskFile)
    {
        Line = line;
        Task = task;
        if (task.Tag("doc") is { } doc && TodoTxt.ResolveDocumentLink(taskFile, doc) is { } link)
        {
            Link = link;
        }
        if (task.Due is { } due)
        {
            string time = task.Time is { } t ? $" {t:HH\\:mm}" : "";
            string day = due == today ? "heute" : due == today.AddDays(1) ? "morgen"
                : due.ToString("ddd dd.MM.", System.Globalization.CultureInfo.GetCultureInfo("de-AT"));
            bool overdue = !task.Done && (due < today || (due == today && task.Time is { } at && at < TimeOnly.FromDateTime(DateTime.Now)));
            DueText = (overdue ? "überfällig · " : "fällig ") + day + time;
            DueBrush = Resource(overdue ? "SystemFillColorCriticalBrush" : due <= today.AddDays(1) ? "AccentTextFillColorPrimaryBrush" : "TextFillColorSecondaryBrush");
        }
    }

    public string Line { get; }

    public TodoTask Task { get; }

    public string Title => Task.Title.Length > 0 ? Task.Title : Task.Description;

    public bool IsDone => Task.Done;

    public TextDecorations Decorations => Task.Done ? TextDecorations.Strikethrough : TextDecorations.None;

    public Brush TitleBrush => Resource(Task.Done ? "TextFillColorSecondaryBrush" : "TextFillColorPrimaryBrush");

    public string DueText { get; } = "";

    public Brush DueBrush { get; } = Resource("TextFillColorSecondaryBrush");

    public Visibility HasDue => DueText.Length > 0 ? Visibility.Visible : Visibility.Collapsed;

    public (string Document, int PageIndex)? Link { get; }

    public Visibility HasLink => Link != null ? Visibility.Visible : Visibility.Collapsed;

    public string LinkText => Link is { } l
        ? Path.GetFileName(l.Document) + (l.PageIndex >= 0 ? $", Seite {l.PageIndex + 1}" : "") : "";

    private static Brush Resource(string key) => (Brush)Application.Current.Resources[key];
}
