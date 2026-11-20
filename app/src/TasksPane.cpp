#include "TasksPane.h"

#include "ChildControls.h"
#include "Questions.h"
#include "TaskEditDialog.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <commctrl.h>
#include <windowsx.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioTasksPane";
        enum ControlId : int { ListId = 10, ShowDoneId, CreateId, FormId = 20 }; // FormId: the new task's text box (UI tests find it by this id)
        enum MenuId : UINT { MenuEdit = 1, MenuOpenLink, MenuDelete };
    }

    TasksPane::TasksPane(HWND parent, Messages show) : messages(std::move(show))
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        if (!GetClassInfoExW(instance, ClassName, &wc))
        {
            wc = { sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = ClassName;
            if (!RegisterClassExW(&wc))
            {
                ThrowLastError("RegisterClassEx(tasks pane)");
            }
        }
        window = CreateWindowExW(WS_EX_CONTROLPARENT, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
            instance, this);
        if (!window)
        {
            ThrowLastError("CreateWindowEx(tasks pane)");
        }
        form = std::make_unique<NewTaskForm>(window, FormId);
        form->submitted = [this] { Add(); };
        list = std::make_unique<TaskListView>(window, ListId);
        list->toggled = [this](TaskRow const& row) { Toggle(row); };
        list->renamed = [this](TaskRow const& row, std::wstring const& text) { Rename(row, text); };
        list->opened = [this](TaskRow const& row) { Open(row); };
        list->menuRequested = [this](TaskRow const& row, POINT screen) { ShowMenu(row, screen); };
        showDoneBox = ChildControls::Create(window, WC_BUTTONW, L"Erledigte anzeigen", WS_TABSTOP | BS_AUTOCHECKBOX, ShowDoneId);
        noFile = ChildControls::Create(window, WC_STATICW, L"", SS_LEFT, 0);
        createButton = ChildControls::Create(window, WC_BUTTONW, L"todo.txt anlegen", WS_TABSTOP | BS_PUSHBUTTON, CreateId);
        Reload();
    }

    TasksPane::~TasksPane()
    {
        if (window && IsWindow(window))
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    }

    void TasksPane::Layout()
    {
        RECT rc;
        GetClientRect(window, &rc);
        int dpi = static_cast<int>(GetDpiForWindow(window));
        auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        int margin = px(8), width = rc.right - 2 * margin, y = margin;
        if (!file || !file->Exists())
        {
            MoveWindow(noFile, margin, y, width, px(40), TRUE);
            MoveWindow(createButton, margin, y + px(44), px(140), px(24), TRUE);
            return;
        }
        y = form->Layout(margin, y, width, dpi);
        // The list from edge to edge (as File Explorer's), "Erledigte anzeigen" below it.
        int boxHeight = px(20), bottom = rc.bottom - margin - boxHeight;
        list->Layout(0, y, rc.right, bottom - y - px(6), dpi);
        MoveWindow(showDoneBox, margin, bottom, width, boxHeight, TRUE);
    }

    void TasksPane::SetFolder(std::optional<std::filesystem::path> const& folder)
    {
        file.reset();
        if (folder)
        {
            file.emplace(*folder / L"todo.txt");
        }
        Reload();
    }

    void TasksPane::SetShowsDone(bool value)
    {
        showDone = value;
        Button_SetCheck(showDoneBox, value ? BST_CHECKED : BST_UNCHECKED);
        Reload();
    }

    void TasksPane::Reload()
    {
        bool exists = false;
        TaskRows rows;
        try
        {
            exists = file && file->Exists();
            if (exists)
            {
                rows = TaskRows::From(file->ReadLines(), file->Path(), showDone);
            }
        }
        catch (...)
        {
            LogException(std::format("tasks: read {}", file ? ToUtf8(file->Path().native()) : ""));
            messages(L"todo.txt konnte nicht gelesen werden (Einzelheiten in errors.log).", ToastKind::Error);
        }
        form->Show(exists);
        ShowWindow(list->Window(), exists ? SW_SHOW : SW_HIDE);
        ShowWindow(showDoneBox, exists ? SW_SHOW : SW_HIDE);
        ShowWindow(noFile, exists ? SW_HIDE : SW_SHOW);
        ShowWindow(createButton, !exists && file ? SW_SHOW : SW_HIDE);
        SetWindowTextW(noFile, file ? L"Im Projektordner gibt es keine todo.txt." : L"Kein Ordner geöffnet (Datei → Ordner öffnen).");
        form->EnableLink(currentPage && currentPage().first.has_value());
        list->Show(rows);
        Layout();
    }

    bool TasksPane::Change(char const* what, std::function<bool(tasks::TaskFile&)> const& change)
    {
        if (!file)
        {
            return false;
        }
        if (refusedWhileSyncing && refusedWhileSyncing())
        {
            Reload(); // e.g. the check box back to what the file says
            return false;
        }
        bool written = false;
        try
        {
            written = change(*file);
            if (!written)
            {
                messages(L"Die Aufgabe wurde inzwischen geändert (andere App oder Synchronisierung); die Liste ist neu geladen.",
                    ToastKind::Warning);
            }
        }
        catch (...)
        {
            LogException(std::format("tasks: {} in {}", what, ToUtf8(file->Path().native())));
            messages(L"todo.txt konnte nicht geschrieben werden (Einzelheiten in errors.log).", ToastKind::Error);
        }
        Reload();
        return written;
    }

    void TasksPane::Add()
    {
        NewTaskForm::Entry entry = form->Read();
        if (!file || Trim(entry.text).empty())
        {
            return;
        }
        TodoTask task = TodoTxt::Create(Trim(entry.text), Today(), entry.due);
        if (entry.linkPage && currentPage)
        {
            auto [document, page] = currentPage();
            if (document)
            {
                task.description = TodoTxt::SetTag(task.description, L"doc", TodoTxt::DocumentLink(file->Path(), *document, page));
            }
        }
        std::wstring line = task.ToString();
        if (Change("add task", [&](tasks::TaskFile& f) { f.Append(line); return true; }))
        {
            form->Clear(); // otherwise what was typed stays
        }
    }

    void TasksPane::Toggle(TaskRow const& row)
    {
        TodoTask changed = row.task.done ? row.task.Reopen() : row.task.Complete(Today());
        Change("complete task", [&](tasks::TaskFile& f) { return f.Replace(row.line, changed.ToString()); });
    }

    // The text as written (with +projects, @contexts and other tags) without due: and doc:, which the dialog shows as a
    // date and a link.
    void TasksPane::Edit(TaskRow const& row)
    {
        std::wstring shown = TaskListView::EditableText(row.task);
        auto [document, page] = currentPage ? currentPage() : std::pair<std::optional<std::filesystem::path>, int>();
        auto values = TaskEditDialog::Show(window, { shown, row.task.Due(), row.link.has_value() }, row.linkText,
            document.has_value());
        if (!values)
        {
            return;
        }
        std::wstring description = values->text;
        if (values->due)
        {
            description = TodoTxt::SetTag(description, L"due", TodoTxt::FormatDate(*values->due));
        }
        if (values->link)
        {
            auto doc = row.task.Tag(L"doc");
            std::optional<std::wstring> link = doc ? doc
                : document ? std::optional(TodoTxt::DocumentLink(file->Path(), *document, page)) : std::nullopt;
            if (link) description = TodoTxt::SetTag(description, L"doc", *link);
        }
        TodoTask changed = row.task;
        changed.description = description;
        Change("edit task", [&](tasks::TaskFile& f) { return f.Replace(row.line, changed.ToString()); });
    }

    // The text edited in place: due: and doc: (not part of the edited text) stay as they were.
    void TasksPane::Rename(TaskRow const& row, std::wstring const& text)
    {
        std::wstring typed(Trim(text));
        if (typed.empty() || typed == TaskListView::EditableText(row.task)) return;
        std::wstring description = typed;
        if (auto due = row.task.Tag(L"due")) description = TodoTxt::SetTag(description, L"due", *due);
        if (auto doc = row.task.Tag(L"doc")) description = TodoTxt::SetTag(description, L"doc", *doc);
        TodoTask changed = row.task;
        changed.description = description;
        Change("edit task in place", [&](tasks::TaskFile& f) { return f.Replace(row.line, changed.ToString()); });
    }

    void TasksPane::Open(TaskRow const& row)
    {
        if (row.link && openRequested)
        {
            openRequested(row.link->first, row.link->second);
        }
    }

    void TasksPane::ShowMenu(TaskRow const& row, POINT screen)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            ThrowLastError("CreatePopupMenu");
        }
        AppendMenuW(menu, MF_STRING, MenuEdit, L"&Bearbeiten…");
        AppendMenuW(menu, MF_STRING | (row.link ? MF_ENABLED : MF_GRAYED), MenuOpenLink, L"Verknüpfte Seite ö&ffnen");
        AppendMenuW(menu, MF_STRING, MenuDelete, L"&Löschen");
        UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, window, nullptr));
        DestroyMenu(menu);
        TaskRow target = row; // the menu ran a message loop: the list may have been refilled meanwhile
        if (chosen == MenuEdit)
        {
            Edit(target);
        }
        else if (chosen == MenuOpenLink)
        {
            Open(target);
        }
        else if (chosen == MenuDelete && Questions::Confirm(window, L"Aufgabe löschen?",
            std::format(L"„{}“ wird aus der todo.txt entfernt.", target.title), L"Löschen"))
        {
            Change("delete task", [&](tasks::TaskFile& f) { return f.Remove(target.line); });
        }
    }

    void TasksPane::CreateTaskFile()
    {
        Change("create todo.txt", [](tasks::TaskFile& f)
        {
            if (!f.Exists()) // another app made one meanwhile: shown, never overwritten
            {
                io::Files::WriteReplacing(f.Path(), {});
            }
            return true;
        });
    }

    LRESULT TasksPane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_COMMAND:
            if (form->OnCommand(LOWORD(wp))) return 0;
            if (LOWORD(wp) == CreateId)
            {
                CreateTaskFile();
                return 0;
            }
            if (LOWORD(wp) == ShowDoneId)
            {
                showDone = Button_GetCheck(showDoneBox) == BST_CHECKED;
                Reload();
                if (showsDoneChanged) showsDoneChanged(showDone);
                return 0;
            }
            break;
        case WM_NOTIFY:
        {
            auto const& note = *reinterpret_cast<NMHDR const*>(lp);
            if (note.hwndFrom == list->Window())
            {
                return list->OnNotify(note);
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
            // The labels and check boxes on the pane's window background (not the dialog color).
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK TasksPane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<TasksPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->window || !self->list)
        {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("tasks pane: message 0x{:X}", message));
            return 0;
        }
    }
}
