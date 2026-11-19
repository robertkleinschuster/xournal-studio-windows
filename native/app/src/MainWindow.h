#pragma once

// The main window: wires the menu, the toolbar, the side pane, the writing surface, the toast and the settings to the
// feature classes (DocumentSession, DocumentView, ProjectExplorer, ExplorerCommands, SyncController, TasksPane,
// RecoveryCopies). It holds no feature logic of its own.

#include "DocumentSession.h"
#include "DocumentView.h"
#include "ExplorerCommands.h"
#include "MainMenu.h"
#include "PageCommands.h"
#include "PdfExportCommand.h"
#include "ProjectExplorer.h"
#include "RecoveryCopies.h"
#include "SidePane.h"
#include "SyncController.h"
#include "Toast.h"

#include "xournal/settings/Settings.h"

#include "inkpages/Toolbar.h"
#include "inkpages/WritingSurface.h"

#include <windows.h>

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
        void UpdateTitle();
        void SetPaneOpen(bool open);
        void OpenFolder();
        void CreateProjectParts();
        void SetProjectFolder(std::optional<std::filesystem::path> const& folder);
        void OnSyncStateChanged();
        void RememberWindow();
        void SaveSettings();
        void Message(std::wstring const& text, ToastKind kind);
        bool Close();

        HWND window = nullptr;
        AppSettings settings;
        MainMenu menu;
        std::unique_ptr<inkpages::WritingSurface> surface;
        std::unique_ptr<inkpages::Toolbar> toolbar;
        std::unique_ptr<SidePane> pane;
        std::unique_ptr<Toast> toast;
        std::unique_ptr<DocumentView> view;
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<PdfExportCommand> exportCommand;
        std::unique_ptr<PageCommands> pageCommands;
        std::unique_ptr<SyncController> sync;
        std::unique_ptr<ProjectExplorer> projectExplorer;
        std::unique_ptr<ExplorerCommands> explorerCommands;
        std::unique_ptr<RecoveryCopies> recovery;
    };
}
