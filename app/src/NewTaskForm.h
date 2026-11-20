#pragma once

// The Aufgaben tab's form for a new task: its text (Enter adds), an optional due date (a check box "Fällig am:" and a date picker enabled while it is checked; none by default),
// "Mit dieser Seite verknüpfen" and the Hinzufügen button. It only collects; TasksPane writes the task.

#include "xournal/model/TodoTxt.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

namespace xournal::app
{
    class NewTaskForm
    {
    public:
        struct Entry
        {
            std::wstring text;
            std::optional<Date> due;
            bool linkPage = false;
        };

        NewTaskForm(HWND parent, int firstId); // uses ids firstId .. firstId + 4
        ~NewTaskForm();
        NewTaskForm(NewTaskForm const&) = delete;
        NewTaskForm& operator=(NewTaskForm const&) = delete;

        /// Places the controls from `top`; returns the y below them.
        int Layout(int left, int top, int width, int dpi);
        void Show(bool shown);
        void EnableLink(bool enabled);

        Entry Read() const;
        void Clear();

        /// The parent forwards WM_COMMAND; true when it was the form's.
        bool OnCommand(int id);

        /// Enter in the text box or Hinzufügen.
        std::function<void()> submitted;

    private:
        static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);

        int addId, hasDueId;
        HWND text = nullptr, hasDue = nullptr, due = nullptr, linkPage = nullptr, addButton = nullptr;
    };
}
