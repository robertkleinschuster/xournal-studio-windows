#pragma once

// The left side pane: a tab control with Dateien (the project folder's tree), Aufgaben and Seiten (to come).

#include "ExplorerTree.h"

#include <windows.h>

#include <functional>
#include <memory>

namespace xournal::app
{
    class SidePane
    {
    public:
        explicit SidePane(HWND parent);
        ~SidePane();
        SidePane(SidePane const&) = delete;
        SidePane& operator=(SidePane const&) = delete;

        HWND Window() const { return window; }
        ExplorerTree& Explorer() { return *explorer; }

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
        HWND tasksPlaceholder = nullptr;
        HWND pagesPlaceholder = nullptr;
        std::unique_ptr<ExplorerTree> explorer;
    };
}
