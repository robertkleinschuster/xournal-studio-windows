#include "MainWindow.h"

#include "AppFolder.h"
#include "AppToolbar.h"
#include "Commands.h"
#include "Dialogs.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioWindow";
        constexpr UINT QueueMessage = WM_APP + 1;
        constexpr int PaneWidthDip = 280;

        // Keys typed into an edit box (the explorer's rename, a new task) are the box's, not the menu's shortcuts
        // (Entf, Strg+C/V/X/A would otherwise act on the document).
        bool TypingInEdit()
        {
            wchar_t name[16]{};
            HWND focus = GetFocus();
            return focus && GetClassNameW(focus, name, ARRAYSIZE(name)) && EqualsIgnoreCase(name, WC_EDITW);
        }
    }

    MainWindow::MainWindow(HINSTANCE instance, AppSettings appSettings, std::optional<std::filesystem::path> startFile)
        : settings(std::move(appSettings))
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = ClassName;
        if (!RegisterClassExW(&wc))
        {
            ThrowLastError("RegisterClassEx(main window)");
        }
        window = CreateWindowExW(0, ClassName, L"Xournal Studio", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, menu.Bar(), instance, this);
        if (!window)
        {
            ThrowLastError("CreateWindowEx(main window)");
        }
        UiQueue::Attach(window, QueueMessage);
        CreateParts(std::move(startFile));
    }

    MainWindow::~MainWindow()
    {
        if (window && IsWindow(window))
        {
            DestroyWindow(window);
        }
    }

    void MainWindow::CreateParts(std::optional<std::filesystem::path> startFile)
    {
        surface = inkpages::WritingSurface::Create(window);
        view = std::make_unique<DocumentView>(*surface);
        session = std::make_unique<DocumentSession>(window, *surface, *view);
        session->changed = [this]
        {
            UpdateTitle();
            UpdateCommands();
            if (explorerCommands) explorerCommands->DocumentShown();
            if (projectExplorer) projectExplorer->Refresh(); // a save changes the git colors
        };
        session->message = [this](std::wstring const& text, ToastKind kind) { Message(text, kind); };
        exportCommand = std::make_unique<PdfExportCommand>(window, *view,
            [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        pageCommands = std::make_unique<PageCommands>(window, *surface, *view);
        view->pagesChanged = [this] { session->NotePagesChanged(); UpdateCommands(); };
        // Set before the toolbar: it chains these handlers.
        surface->historyChanged = [this] { UpdateTitle(); UpdateCommands(); };
        surface->selectionChanged = [] {};

        toolbar = CreateAppToolbar(window, *surface, inkpages::ToolChoices::Parse(settings.toolChoices), {
            [this] { OnCommand(CmdSave); },
            [this] { OnCommand(CmdExportPdf); },
            [this] { OnCommand(CmdInsertPage); },
            [this] { OnCommand(CmdDeletePage); },
        });
        toolbar->choicesChanged = [this](inkpages::ToolChoices const& choices)
        {
            settings.toolChoices = choices.Serialize();
            SaveSettings();
        };

        pane = std::make_unique<SidePane>(window, [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        pane->SetTab(settings.paneTab);
        pane->tabChanged = [this](int tab)
        {
            settings.paneTab = tab;
            SaveSettings();
        };
        toast = std::make_unique<Toast>(window);
        CreateProjectParts();

        if (settings.window)
        {
            WINDOWPLACEMENT placement{ sizeof(placement) };
            placement.rcNormalPosition = settings.window->normal;
            placement.showCmd = settings.window->maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            if (!SetWindowPlacement(window, &placement))
            {
                ThrowLastError("SetWindowPlacement");
            }
        }
        else
        {
            ShowWindow(window, SW_SHOWMAXIMIZED);
        }
        SetPaneOpen(settings.paneOpen);
        Layout();

        if (!recovery->OfferAtStart())
        {
            auto file = startFile ? startFile : settings.lastFile;
            if (file && std::filesystem::exists(*file))
            {
                session->Open(*file);
            }
            else
            {
                session->New();
            }
        }
        UpdateTitle();
    }

    int MainWindow::Run()
    {
        MSG message;
        BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        {
            if (TypingInEdit() || !TranslateAcceleratorW(window, menu.Accelerators(), &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (result < 0)
        {
            ThrowLastError("GetMessage");
        }
        return static_cast<int>(message.wParam);
    }

    void MainWindow::Layout()
    {
        if (!surface || !toolbar || !pane)
        {
            return;
        }
        RECT rc;
        GetClientRect(window, &rc);
        int top = toolbar->Height();
        MoveWindow(toolbar->Window(), 0, 0, rc.right, top, TRUE);
        int paneWidth = settings.paneOpen ? MulDiv(PaneWidthDip, static_cast<int>(GetDpiForWindow(window)), 96) : 0;
        MoveWindow(pane->Window(), 0, top, paneWidth, rc.bottom - top, TRUE);
        MoveWindow(surface->Window(), paneWidth, top, rc.right - paneWidth, rc.bottom - top, TRUE);
        RECT area;
        GetWindowRect(surface->Window(), &area);
        toast->Place(area);
    }

    void MainWindow::SetPaneOpen(bool open)
    {
        settings.paneOpen = open;
        ShowWindow(pane->Window(), open ? SW_SHOW : SW_HIDE);
        menu.Check(CmdTogglePane, open);
        Layout();
    }

    void MainWindow::UpdateTitle()
    {
        if (!session) return;
        SetWindowTextW(window, session->Title().c_str());
        settings.lastFile = session->FilePath();
        if (recovery) recovery->Update();
    }

    void MainWindow::UpdateCommands()
    {
        if (!toolbar || !view) return;
        bool deletable = view->PageCount() > 1;
        toolbar->EnableButton(CmdDeletePage, deletable);
        menu.Enable(CmdDeletePage, deletable);
    }

    void MainWindow::UpdateMenu()
    {
        bool selection = surface->HasSelection();
        menu.Enable(CmdUndo, surface->CanUndo());
        menu.Enable(CmdRedo, surface->CanRedo());
        for (int command : { CmdCut, CmdCopy, CmdDuplicate, CmdDelete })
        {
            menu.Enable(command, selection);
        }
        menu.Enable(CmdSync, !sync->IsSyncing());
    }

    void MainWindow::OpenFolder()
    {
        auto folder = Dialogs::PickFolder(window, settings.projectFolder);
        if (!folder) return;
        settings.projectFolder = *folder;
        SetProjectFolder(folder);
        SetPaneOpen(true);
        pane->SetTab(0);
        SaveSettings();
    }

    // ---- project folder: explorer, tasks, sync, recovery copies

    void MainWindow::CreateProjectParts()
    {
        auto messages = [this](std::wstring const& text, ToastKind kind) { Message(text, kind); };
        sync = std::make_unique<SyncController>(window, *session, messages);
        sync->stateChanged = [this] { OnSyncStateChanged(); };
        session->refusedWhileSyncing = [this](std::wstring const& what) { return sync->RefusedWhileSyncing(what); };
        session->saveDeferredWhileSyncing = [this] { return sync->DeferSave(); };

        projectExplorer = std::make_unique<ProjectExplorer>(pane->Explorer());
        projectExplorer->refreshed = [this] { pane->Tasks().Reload(); };
        explorerCommands = std::make_unique<ExplorerCommands>(window, *session, *sync, *projectExplorer, pane->Explorer(), messages);
        explorerCommands->focusDocument = [this] { SetFocus(surface->Window()); };
        explorerCommands->scrollToPage = [this](int page) { surface->ScrollToPage(page); };
        pane->Explorer().requested = [this](ExplorerRequest const& request) { explorerCommands->Run(request); };
        pane->syncRequested = [this] { OnCommand(CmdSync); };

        TasksPane& tasks = pane->Tasks();
        tasks.currentPage = [this] { return std::pair(session->FilePath(), surface->PageInView().value_or(-1)); };
        tasks.refusedWhileSyncing = [this] { return sync->RefusedWhileSyncing(L"Das Ändern der Aufgaben"); };
        tasks.openRequested = [this](std::filesystem::path const& document, int page) { explorerCommands->Open(document, page); };
        tasks.showsDoneChanged = [this](bool shown)
        {
            settings.doneTasksShown = shown;
            SaveSettings();
        };
        tasks.SetShowsDone(settings.doneTasksShown);
        SetProjectFolder(settings.projectFolder);

        recovery = std::make_unique<RecoveryCopies>(window, AppFolder::Path(), *session, *view, messages);
    }

    void MainWindow::SetProjectFolder(std::optional<std::filesystem::path> const& folder)
    {
        projectExplorer->SetFolder(folder);
        pane->Tasks().SetFolder(folder);
    }

    void MainWindow::OnSyncStateChanged()
    {
        pane->SetSyncing(sync->IsSyncing());
        if (!sync->IsSyncing())
        {
            projectExplorer->Refresh();
        }
    }

    void MainWindow::OnCommand(int command)
    {
        switch (command)
        {
        case CmdNew: session->New(); break;
        case CmdOpen: session->Open(); break;
        case CmdSave: session->Save(); break;
        case CmdSaveAs: session->SaveAs(); break;
        case CmdExportPdf: exportCommand->Export(session->Document()); break;
        case CmdInsertPage: pageCommands->InsertAfterCurrent(); break;
        case CmdDeletePage: pageCommands->DeleteCurrent(); break;
        case CmdOpenFolder: OpenFolder(); break;
        case CmdSync: sync->Sync(projectExplorer->Folder()); break;
        case CmdExit: PostMessageW(window, WM_CLOSE, 0, 0); break;
        case CmdUndo: surface->Undo(); break;
        case CmdRedo: surface->Redo(); break;
        case CmdCut: surface->Cut(); break;
        case CmdCopy: surface->Copy(); break;
        case CmdPaste: surface->Paste(); break;
        case CmdDuplicate: surface->Duplicate(); break;
        case CmdDelete:
            if (pane->Explorer().HasFocus()) pane->Explorer().RequestForSelection(ExplorerCommand::Delete); // Entf in the tree
            else surface->DeleteSelection();
            break;
        case CmdSelectAll: surface->SelectAll(); break;
        case CmdTogglePane: SetPaneOpen(!settings.paneOpen); SaveSettings(); break;
        case CmdFitWidth: surface->FitWidth(); break;
        case CmdZoomIn: surface->ZoomTo(surface->CurrentView().zoom * 1.25f); break;
        case CmdZoomOut: surface->ZoomTo(surface->CurrentView().zoom / 1.25f); break;
        default:
            Log(std::format("command {} is not available yet", command)); // disabled in the menu; an accelerator may still come
            break;
        }
    }

    void MainWindow::Message(std::wstring const& text, ToastKind kind)
    {
        RECT area;
        GetWindowRect(surface->Window(), &area);
        toast->Place(area);
        toast->Show(text, kind);
    }

    void MainWindow::RememberWindow()
    {
        WINDOWPLACEMENT placement{ sizeof(placement) };
        if (!GetWindowPlacement(window, &placement))
        {
            ThrowLastError("GetWindowPlacement");
        }
        settings.window = settings::WindowState{ placement.rcNormalPosition, placement.showCmd == SW_SHOWMAXIMIZED };
    }

    void MainWindow::SaveSettings()
    {
        try
        {
            settings.Save();
        }
        catch (...)
        {
            LogException("settings: save");
            Message(L"Die Einstellungen konnten nicht gespeichert werden.", ToastKind::Error);
        }
    }

    bool MainWindow::Close()
    {
        if (!session->ConfirmLeave())
        {
            return false;
        }
        recovery->End(); // saved or discarded: nothing to recover
        RememberWindow();
        settings.lastFile = session->FilePath();
        SaveSettings();
        return true;
    }

    LRESULT MainWindow::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_SIZE:
        case WM_MOVE:
            Layout();
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wp));
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
            {
                if (recovery) recovery->Deactivated(); // maybe for good (shutdown, log off): the copy now
            }
            else if (projectExplorer)
            {
                projectExplorer->Refresh(); // files may have changed in another app
            }
            break; // and the default handling (focus)
        case WM_INITMENUPOPUP:
            UpdateMenu();
            return 0;
        case QueueMessage:
            UiQueue::Run(lp);
            return 0;
        case WM_CLOSE:
            if (Close())
            {
                DestroyWindow(window);
            }
            return 0;
        case WM_DESTROY:
            // The parts go before the window's children do (the surface's ink thread hands its last pages back).
            recovery.reset();
            explorerCommands.reset();
            projectExplorer.reset();
            sync.reset();
            // The feature classes that hold the surface go before it (the view takes its callbacks back).
            pageCommands.reset();
            exportCommand.reset();
            session.reset();
            view.reset();
            toolbar.reset();
            surface.reset();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK MainWindow::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            auto self = reinterpret_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
        {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("main window: message 0x{:X}", message));
            if (self->toast)
            {
                self->toast->Show(L"Ein Fehler ist aufgetreten (Einzelheiten in errors.log).", ToastKind::Error);
            }
            return 0;
        }
    }
}
