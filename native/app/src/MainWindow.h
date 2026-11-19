#pragma once

// The main window: wires the menu, the toolbar, the side pane, the writing surface, the toast and the settings to the
// feature classes (DocumentSession, DocumentView, ExplorerTree). It holds no feature logic of its own.

#include "DocumentSession.h"
#include "DocumentView.h"
#include "MainMenu.h"
#include "PdfExportCommand.h"
#include "Settings.h"
#include "SidePane.h"
#include "Toast.h"

#include "inkpages/Toolbar.h"
#include "inkpages/WritingSurface.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <optional>

namespace xournal::app
{
    class MainWindow
    {
    public:
        /// `startFile`: a document to open (command line); otherwise the last one, else a new document.
        MainWindow(HINSTANCE instance, Settings settings, std::optional<std::filesystem::path> startFile);
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
        void UpdateTitle();
        void SetPaneOpen(bool open);
        void OpenFolder();
        void OpenFromExplorer(std::filesystem::path const& file);
        void RememberWindow();
        void SaveSettings();
        void Message(std::wstring const& text, ToastKind kind);
        bool Close();

        HWND window = nullptr;
        Settings settings;
        MainMenu menu;
        std::unique_ptr<inkpages::WritingSurface> surface;
        std::unique_ptr<inkpages::Toolbar> toolbar;
        std::unique_ptr<SidePane> pane;
        std::unique_ptr<Toast> toast;
        std::unique_ptr<DocumentView> view;
        std::unique_ptr<DocumentSession> session;
        std::unique_ptr<PdfExportCommand> exportCommand;
    };
}
