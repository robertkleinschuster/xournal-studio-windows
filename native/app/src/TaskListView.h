#pragma once

// The Aufgaben tab's list (comctl32 ListView, report view with check boxes): one row per task - its title (with its
// linked page) and due text; overdue due dates in red, soon ones in the accent color, done tasks gray. The check box,
// a double-click and the context menu are reported with the row; the list never changes the file itself.

#include "TaskRows.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>

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

        /// The check box was switched (reported after the list's own notification: the handler may refill the list).
        std::function<void(TaskRow const&)> toggled;
        std::function<void(TaskRow const&)> opened;
        std::function<void(TaskRow const&, POINT screen)> menuRequested;

    private:
        TaskRow const* RowAt(int index) const;

        HWND list = nullptr;
        std::vector<TaskRow> rows;
        bool filling = false; // the check boxes change while the list is filled: not the user's
    };
}
