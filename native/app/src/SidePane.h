#pragma once

// The left side pane: a tab control with Dateien (the project folder's tree and the sync button), Aufgaben (todo.txt)
// and Seiten (the pages pane, put in by the window).

#include "ExplorerTree.h"
#include "TasksPane.h"

#include <windows.h>

#include <functional>
#include <memory>

namespace xournal::app
{
    class SidePane
    {
    public:
        SidePane(HWND parent, TasksPane::Messages messages);
        ~SidePane();
        SidePane(SidePane const&) = delete;
        SidePane& operator=(SidePane const&) = delete;

        HWND Window() const { return window; }
        ExplorerTree& Explorer() { return *explorer; }
        TasksPane& Tasks() { return *tasks; }

        /// The sync button was pressed.
        std::function<void()> syncRequested;
        /// While a sync runs the button says so and waits.
        void SetSyncing(bool syncing);

        /// The Seiten tab's content (a child window of this pane), in place of its placeholder.
        void SetPagesContent(HWND content);

        int Tab() const;
        void SetTab(int tab);
        /// The user chose another tab.
        std::function<void(int)> tabChanged;

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void Layout();
        void ShowPage(int tab);

        HWND window = nullptr;
        HWND tabs = nullptr;
        HWND syncButton = nullptr;
        HWND pagesContent = nullptr; // a placeholder until the window puts the pages pane in
        std::unique_ptr<ExplorerTree> explorer;
        std::unique_ptr<TasksPane> tasks;
    };
}
