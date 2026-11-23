#pragma once

// The left side pane: a tab control with Dateien (the project folder's tree and its toolbar), Seiten (the pages pane),
// Gliederung (the outline pane, only while the document's PDF has an outline), Suche (the search pane) and Aufgaben
// (todo.txt) - in that order; the window puts the pages, outline and search panes in. Each tab has a fixed id
// (Tab()/SetTab() use it, also the saved setting), independent of its place in the strip.

#include "ExplorerTree.h"
#include "TasksPane.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <memory>
#include <string>

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
        void SetDarkMode(bool dark);

        /// The sync button was pressed.
        std::function<void()> syncRequested;
        /// The pull button was pressed (fetch and bring the server's commits in, no upload).
        std::function<void()> pullRequested;
        /// While a sync runs the button says so and waits.
        void SetSyncing(bool syncing);
        /// The button's tooltip after "Synchronisieren" (one fact per line); the counts are the status bar's.
        void SetSyncDetails(std::wstring const& details);
        void SetCurrentFile(std::optional<std::filesystem::path> file);
        void UpdateCurrentFileButton();

        /// The Seiten tab's content (a child window of this pane), in place of its placeholder.
        void SetPagesContent(HWND content);
        /// The Suche tab's content (a child window of this pane).
        void SetSearchContent(HWND content);
        /// The Gliederung tab's content (a child window of this pane) and whether the tab is shown.
        void SetOutlineContent(HWND content);
        void ShowOutlineTab(bool shown);

        static constexpr int PagesTab = 2, SearchTab = 3;
        int Tab() const;
        void SetTab(int tab);
        /// The user chose another tab.
        std::function<void(int)> tabChanged;
        /// The width (pixels) the tab strip needs to show every tab in one row without scroll arrows - Gliederung
        /// included even while it is hidden, so the pane does not change width with the document. Measured from the tab
        /// control's own item rectangles and its font.
        int TabsWidth(bool includeOutline = true) const;

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void Layout();
        void ShowPage(int tab);
        void CreateFileToolbar();
        void UpdateFileToolbarImages();

        HWND window = nullptr;
        HWND tabs = nullptr;
        HWND fileBar = nullptr;
        std::unique_ptr<std::remove_pointer_t<HIMAGELIST>, decltype(&ImageList_Destroy)> fileBarImages{ nullptr, ImageList_Destroy };
        HWND syncTip = nullptr;
        std::optional<std::filesystem::path> currentFile;
        std::wstring syncDetails;
        bool syncing = false;
        bool darkMode = false;
        HWND pagesContent = nullptr; // a placeholder until the window puts the pages pane in
        HWND searchContent = nullptr;
        HWND outlineContent = nullptr;
        std::unique_ptr<ExplorerTree> explorer;
        std::unique_ptr<TasksPane> tasks;
    };
}
