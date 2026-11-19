#include "MainWindow.h"

#include "AppToolbar.h"
#include "Commands.h"
#include "Dialogs.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <shellapi.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioWindow";
        constexpr UINT QueueMessage = WM_APP + 1;
        constexpr int PaneWidthDip = 280;

        bool IsDocument(std::filesystem::path const& file)
        {
            auto extension = file.extension().native();
            return EqualsIgnoreCase(extension, L".xopp") || EqualsIgnoreCase(extension, L".moj") || EqualsIgnoreCase(extension, L".pdf");
        }
    }

    MainWindow::MainWindow(HINSTANCE instance, Settings appSettings, std::optional<std::filesystem::path> startFile)
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
        session->changed = [this] { UpdateTitle(); };
        session->message = [this](std::wstring const& text, ToastKind kind) { Message(text, kind); };
        exportCommand = std::make_unique<PdfExportCommand>(window, *view,
            [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        // Set before the toolbar: it chains these handlers.
        surface->historyChanged = [this] { UpdateTitle(); };
        surface->selectionChanged = [] {};

        toolbar = CreateAppToolbar(window, *surface, inkpages::ToolChoices::Parse(settings.toolChoices), {
            [this] { OnCommand(CmdInsertPage); },
            [this] { OnCommand(CmdDeletePage); },
        });
        // Inserting and deleting pages wait for InkPages to insert pages without clearing the undo history.
        toolbar->EnableButton(CmdInsertPage, false);
        toolbar->EnableButton(CmdDeletePage, false);
        toolbar->choicesChanged = [this](inkpages::ToolChoices const& choices)
        {
            settings.toolChoices = choices.Serialize();
            SaveSettings();
        };

        pane = std::make_unique<SidePane>(window);
        pane->SetTab(settings.paneTab);
        pane->tabChanged = [this](int tab)
        {
            settings.paneTab = tab;
            SaveSettings();
        };
        pane->Explorer().fileChosen = [this](std::filesystem::path const& file) { OpenFromExplorer(file); };
        pane->Explorer().SetFolder(settings.projectFolder);
        toast = std::make_unique<Toast>(window);

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

        auto file = startFile ? startFile : settings.lastFile;
        if (file && std::filesystem::exists(*file))
        {
            session->Open(*file);
        }
        else
        {
            session->New();
        }
        UpdateTitle();
    }

    int MainWindow::Run()
    {
        MSG message;
        BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
        {
            if (!TranslateAcceleratorW(window, menu.Accelerators(), &message))
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
    }

    void MainWindow::OpenFolder()
    {
        auto folder = Dialogs::PickFolder(window, settings.projectFolder);
        if (!folder) return;
        settings.projectFolder = *folder;
        pane->Explorer().SetFolder(folder);
        SetPaneOpen(true);
        pane->SetTab(0);
        SaveSettings();
    }

    void MainWindow::OpenFromExplorer(std::filesystem::path const& file)
    {
        if (IsDocument(file))
        {
            session->Open(file);
            return;
        }
        // Other files open in their Windows app.
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOASYNC;
        info.hwnd = window;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info))
        {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            LogError(std::format("explorer: open {}: {}", ToUtf8(file.native()), DescribeHr(hr)));
            Message(std::format(L"„{}“ lässt sich nicht öffnen.", file.filename().native()), ToastKind::Error);
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
        case CmdOpenFolder: OpenFolder(); break;
        case CmdExit: PostMessageW(window, WM_CLOSE, 0, 0); break;
        case CmdUndo: surface->Undo(); break;
        case CmdRedo: surface->Redo(); break;
        case CmdCut: surface->Cut(); break;
        case CmdCopy: surface->Copy(); break;
        case CmdPaste: surface->Paste(); break;
        case CmdDuplicate: surface->Duplicate(); break;
        case CmdDelete: surface->DeleteSelection(); break;
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
        settings.window = WindowState{ placement.rcNormalPosition, placement.showCmd == SW_SHOWMAXIMIZED };
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
            // The feature classes that hold the surface go before it (the view takes its callbacks back).
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
