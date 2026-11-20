#pragma once

// The Aufgaben tab's list: a comctl32 ListView in report view as File Explorer shows lists (Explorer theme, column
// headers "Aufgabe" / "Fällig", check boxes = done, full-row selection) with group view: Überfällig, Offen, Erledigt -
// each group header with its count. The title shows the task's priority as todo.txt writes it ("(A) ...") and its
// linked page; the due column "heute 10:00", "morgen", "Mo 24.11." (overdue red, today/tomorrow in the accent color,
// done tasks gray); the info tip has it all. F2 or a click on the selected row edits the text in place. The check box,
// the in-place edit, a double-click and the context menu are reported with the row; the list never changes the file.

#include "TaskRows.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>

namespace xournal::app
{
    class TaskListView
    {
    public:
        TaskListView(HWND parent, int id);
        TaskListView(TaskListView const&) = delete;
        TaskListView& operator=(TaskListView const&) = delete;

        HWND Window() const { return list; }
        void Show(TaskRows const& rows);
        void Layout(int left, int top, int width, int height, int dpi);

        /// The parent forwards the list's WM_NOTIFY here.
        LRESULT OnNotify(NMHDR const& note);

        /// The text a task's in-place edit starts with: its description without the due: and doc: tags (shown in the
        /// due column and as the link).
        static std::wstring EditableText(TodoTask const& task);

        /// The check box was switched (reported after the list's own notification: the handler may refill the list).
        std::function<void(TaskRow const&)> toggled;
        /// The text was edited in place (`text` as typed; reported after the list's notification, like toggled).
        std::function<void(TaskRow const&, std::wstring const& text)> renamed;
        std::function<void(TaskRow const&)> opened;
        std::function<void(TaskRow const&, POINT screen)> menuRequested;

    private:
        TaskRow const* RowOfItem(int item) const;
        LRESULT OnCustomDraw(NMLVCUSTOMDRAW& draw);
        void FitColumns();

        HWND list = nullptr;
        std::vector<TaskRow> rows;
        bool filling = false; // the check boxes change while the list is filled: not the user's
    };
}
