#include "TasksPane.h"

#include "ChildControls.h"
#include "Questions.h"

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
        enum ControlId : int { FormId = 1, ListId = 10, ShowDoneId, CreateId };
        enum MenuId : UINT { MenuOpenLink = 1, MenuDelete };
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
        header = ChildControls::Create(window, WC_STATICW, L"Aufgaben", SS_LEFT | SS_ENDELLIPSIS, 0);
        form = std::make_unique<NewTaskForm>(window, FormId);
        form->submitted = [this] { Add(); };
        list = std::make_unique<TaskListView>(window, ListId);
        list->toggled = [this](TaskRow const& row) { Toggle(row); };
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
        MoveWindow(header, margin, y, width, px(20), TRUE);
        y += px(24);
        if (!file || !file->Exists())
        {
            MoveWindow(noFile, margin, y, width, px(40), TRUE);
            MoveWindow(createButton, margin, y + px(44), px(140), px(28), TRUE);
            return;
        }
        y = form->Layout(margin, y, width, dpi);
        int bottom = rc.bottom - margin - px(22);
        list->Layout(margin, y, width, bottom - y - px(4), dpi);
        MoveWindow(showDoneBox, margin, bottom, width, px(22), TRUE);
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
        std::wstring title = !exists ? L"Aufgaben"
            : rows.openCount == 1 ? L"todo.txt – 1 offen" : std::format(L"todo.txt – {} offen", rows.openCount);
        SetWindowTextW(header, title.c_str());
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
        AppendMenuW(menu, MF_STRING | (row.link ? MF_ENABLED : MF_GRAYED), MenuOpenLink, L"Verknüpfte Seite ö&ffnen");
        AppendMenuW(menu, MF_STRING, MenuDelete, L"&Löschen");
        UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, window, nullptr));
        DestroyMenu(menu);
        TaskRow target = row; // the menu ran a message loop: the list may have been refilled meanwhile
        if (chosen == MenuOpenLink)
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
