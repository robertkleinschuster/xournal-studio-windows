#pragma once

// The Aufgaben tab: the project folder's todo.txt (services' TaskFile) shown in a TaskListView with a NewTaskForm above
// it and "Erledigte anzeigen" below. The pane makes the changes: a new task (with an optional due date and a link to the
// page in view, doc: tag as the GTK Xournal Studio wrote), completing or reopening a task with its check box, deleting it
// from the context menu (asked), editing it (TaskEditDialog), opening its linked page. Every change rewrites only its own line; nothing is written
// while a sync runs.

#include "NewTaskForm.h"
#include "TaskListView.h"
#include "TaskRows.h"
#include "Toast.h"

#include "xournal/tasks/TaskFile.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace xournal::app
{
    class TasksPane
    {
    public:
        using Messages = std::function<void(std::wstring const&, ToastKind)>;

        TasksPane(HWND parent, Messages messages);
        ~TasksPane();
        TasksPane(TasksPane const&) = delete;
        TasksPane& operator=(TasksPane const&) = delete;

        HWND Window() const { return window; }

        /// Shows the todo.txt of the folder (again: after changes on disk).
        void SetFolder(std::optional<std::filesystem::path> const& folder);
        void Reload();

        bool ShowsDone() const { return showDone; }
        void SetShowsDone(bool value);

        /// The open document (nothing if never saved) and the page in view, for links.
        std::function<std::pair<std::optional<std::filesystem::path>, int>()> currentPage;
        /// True (and said) while a sync runs: the file is not changed then.
        std::function<bool()> refusedWhileSyncing;
        /// A task's linked page should be opened (document, 0-based page or -1).
        std::function<void(std::filesystem::path const&, int)> openRequested;
        /// The user switched done tasks on or off (a setting).
        std::function<void(bool)> showsDoneChanged;

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void Layout();
        bool Change(char const* what, std::function<bool(tasks::TaskFile&)> const& change);
        void Add();
        void Toggle(TaskRow const& row);
        void Edit(TaskRow const& row);
        void Open(TaskRow const& row);
        void ShowMenu(TaskRow const& row, POINT screen);
        void CreateTaskFile();

        HWND window = nullptr;
        HWND header = nullptr, showDoneBox = nullptr, noFile = nullptr, createButton = nullptr;
        std::unique_ptr<NewTaskForm> form;
        std::unique_ptr<TaskListView> list;
        Messages messages;
        std::optional<tasks::TaskFile> file;
        bool showDone = false;
    };
}
