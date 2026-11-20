#pragma once

// The main window: wires the menu, toolbar, side pane, writing surface, status bar and settings to the
// feature classes (DocumentSession, DocumentView, ProjectExplorer, ExplorerCommands, SyncController, TasksPane,
// RecoveryCopies). It holds no feature logic of its own.

#include "DocumentSession.h"
#include "DocumentView.h"
#include "ExplorerCommands.h"
#include "MainMenu.h"
#include "InsertPagesCommand.h"
#include "LinkHover.h"
#include "PageCommands.h"
#include "PageThumbnails.h"
#include "PagesPane.h"
#include "PdfExportCommand.h"
#include "PdfImportCommand.h"
#include "DocumentSearch.h"
#include "PdfLinks.h"
#include "PdfTextSelection.h"
#include "SearchPane.h"
#include "SelectionClipboard.h"
#include "PdfOutline.h"
#include "ProjectExplorer.h"
#include "RecoveryCopies.h"
#include "RemoteFetch.h"
#include "SidePane.h"
#include "StatusBar.h"
#include "SyncController.h"
#include "MessageKind.h"
#include "ViewNavigation.h"

#include "xournal/settings/Settings.h"

#include "inkpages/Toolbar.h"
#include "inkpages/WritingSurface.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace xournal::app
{
    using AppSettings = settings::Settings;

    class MainWindow
    {
    public:
        /// `startFile`: a document to open (command line); otherwise the last one, else a new document.
        MainWindow(HINSTANCE instance, AppSettings settings, std::optional<std::filesystem::path> startFile);
        ~MainWindow();
        MainWindow(MainWindow const&) = delete;
        MainWindow& operator=(MainWindow const&) = delete;

        /// Runs the message loop until the window is closed; returns the exit code.
        int Run();

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void CreateParts(std::optional<std::filesystem::path> startFile);
        void Layout();
        void OnCommand(int command);
        void UpdateMenu();
        void UpdateCommands(); // commands that depend on the document (a page can be deleted while there are two)
        void CreatePagesPane();
        void CreateSearch();      // Strg+F: the Suche tab and the document search
        void ShowSearch();        // opens the pane on the Suche tab, the keyboard in its field
        void UpdatePagesPane(); // thumbnails only while the Seiten tab is shown
        void UpdateTitle();
        void SetPaneOpen(bool open);
        void OpenFolder();
        void CreateProjectParts();
        void SetProjectFolder(std::optional<std::filesystem::path> const& folder);
        void OnSyncStateChanged();
        void UpdateSyncButton();
        void RememberWindow();
        void SaveSettings();
        void Message(std::wstring const& text, MessageKind kind);
        void CopyPdfText(); // Strg+C with the PDF's text selected (a failure shown)
        // The viewer's parts: the status bar's page and zoom, the shown document's position remembered by file, the
        // pages' context menu, files dropped on the window.
        void UpdateStatus();
        void RememberPlace();
        void ShowPageMenu(LPARAM lp);
        void OpenDropped(HDROP drop);
        void OpenRecent(int index);
        bool Close();

        HWND window = nullptr;
        AppSettings settings;
        MainMenu menu;
        std::unique_ptr<inkpages::WritingSurface> surface;
        std::unique_ptr<inkpages::Toolbar> toolbar;
        std::unique_ptr<SidePane> pane;
        std::unique_ptr<StatusBar> statusBar;
        std::unique_ptr<ViewNavigation> navigation;
        std::unique_ptr<LinkHover> linkHover;
        HWND focusBefore = nullptr; // the focused child when the window was deactivated (given back on activation)
        std::unique_ptr<DocumentView> view;
        std::unique_ptr<SelectionClipboard> clipboard;
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<PdfExportCommand> exportCommand;
        std::unique_ptr<PageCommands> pageCommands;
        std::unique_ptr<InsertPagesCommand> insertCommand;
        std::unique_ptr<PdfImportCommand> importCommand;
        std::unique_ptr<PageThumbnails> thumbnails;
        std::unique_ptr<PagesPane> pagesPane;
        std::unique_ptr<PdfLinks> links;
        std::unique_ptr<PdfTextSelection> textSelection;
        std::unique_ptr<PdfOutline> outline;
        std::unique_ptr<DocumentSearch> search;
        std::unique_ptr<SearchPane> searchPane;
        std::unique_ptr<SyncController> sync;
        std::unique_ptr<RemoteFetch> remoteFetch;
        std::unique_ptr<ProjectExplorer> projectExplorer;
        std::unique_ptr<ExplorerCommands> explorerCommands;
        std::unique_ptr<RecoveryCopies> recovery;
    };
}
