#include "MainWindow.h"

#include "AppFolder.h"
#include "AppToolbar.h"
#include "Commands.h"
#include "Dialogs.h"
#include "ErrorText.h"
#include "FileDrop.h"
#include "PageContextMenu.h"
#include "Resources.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
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
        // The app's icon at a size the system asks for (LoadIconMetric scales the best image of app.ico for the DPI).
        HICON AppIcon(HINSTANCE instance, _LI_METRIC size)
        {
            HICON icon = nullptr;
            HRESULT hr = LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), size, &icon);
            if (FAILED(hr))
            {
                LogError(std::format("LoadIconMetric (app icon): {}", DescribeHr(hr))); // the window shows the default icon
                return nullptr;
            }
            return icon;
        }

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
        wc.hIcon = AppIcon(instance, LIM_LARGE);
        wc.hIconSm = AppIcon(instance, LIM_SMALL);
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
        navigation = std::make_unique<ViewNavigation>(window, *surface);
        view = std::make_unique<DocumentView>(*surface);
        session = std::make_unique<DocumentSession>(window, *surface, *view);
        // The last position of each document (by its file, or the PDF it was opened from) and the recent files.
        session->leaving = [this] { RememberPlace(); };
        session->placeOf = [this](std::filesystem::path const& key) -> std::optional<inkpages::ViewPlace>
        {
            auto it = settings.documentPositions.find(key.native());
            if (it == settings.documentPositions.end()) return std::nullopt;
            auto const& p = it->second;
            return inkpages::ViewPlace{ p.page, { static_cast<float>(p.x), static_cast<float>(p.y) }, static_cast<float>(p.zoom) };
        };
        session->fileUsed = [this](std::filesystem::path const& file)
        {
            settings.RememberRecent(file);
            menu.SetRecentFiles(settings.recentFiles);
            SaveSettings();
        };
        menu.SetRecentFiles(settings.recentFiles);
        view->pagesRemapped = [this](inkpages::PageMap const& map) { navigation->PagesRemapped(map); };
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
        insertCommand = std::make_unique<InsertPagesCommand>(window, *session, *view,
            [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        importCommand = std::make_unique<PdfImportCommand>(window, *session,
            [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        view->pagesChanged = [this]
        {
            session->NotePagesChanged();
            UpdateCommands();
            if (thumbnails) thumbnails->PagesChanged();
            if (pagesPane) pagesPane->SetCount(view->PageCount());
            if (search) search->PagesChanged();
            textSelection->Clear();
            UpdateStatus();
        };
        // Set before the toolbar: it chains these handlers.
        surface->historyChanged = [this] { if (session) session->NoteEdit(); UpdateTitle(); UpdateCommands(); };
        // The mouse selects the PDF's text (a drag on it, double/triple click); a selection of ink clears it.
        textSelection = std::make_unique<PdfTextSelection>(window, *view);
        surface->selectionChanged = [this] { textSelection->Clear(); };
        surface->mouseClaims = [this](int page, inkpages::PointF point) { return textSelection->TextAt(page, point); };
        surface->mouseGesture = [this](inkpages::MouseGesture const& gesture) { textSelection->Gesture(gesture); };
        surface->textEditingChanged = [this](bool) { textSelection->Clear(); };
        clipboard = std::make_unique<SelectionClipboard>(window, *surface, *view);
        surface->copyToClipboard = [this](inkpages::Clip const& clip)
        {
            try
            {
                clipboard->Copy(clip);
                return true;
            }
            catch (...)
            {
                LogException("clipboard: copy");
                Message(std::format(L"Kopieren fehlgeschlagen: {}", ErrorText()), ToastKind::Error);
                return false; // Ausschneiden keeps the selection
            }
        };
        surface->pasteFromClipboard = [this](inkpages::PointF at) -> std::optional<inkpages::Clip>
        {
            try
            {
                return clipboard->Paste(at);
            }
            catch (...)
            {
                LogException("clipboard: paste");
                Message(std::format(L"Einfügen fehlgeschlagen: {}", ErrorText()), ToastKind::Error);
                return inkpages::Clip{}; // nothing pasted (not Windows ink instead)
            }
        };

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
            textSelection->Clear(); // another tool (or its settings)
        };

        pane = std::make_unique<SidePane>(window, [this](std::wstring const& text, ToastKind kind) { Message(text, kind); });
        pane->SetTab(settings.paneTab);
        pane->tabChanged = [this](int tab)
        {
            settings.paneTab = tab;
            SaveSettings();
            UpdatePagesPane();
        };
        CreatePagesPane();
        // The background PDF's outline (Gliederung) and links: a mouse click or finger tap follows a link.
        links = std::make_unique<PdfLinks>(window, *surface, *view);
        links->jumping = [this] { navigation->Remember(); }; // links and outline entries: Zurück returns
        linkHover = std::make_unique<LinkHover>(window, *links);
        // The cursor over the pages: the hand over a link (with its tooltip), the I-beam over the PDF's text.
        surface->cursorAt = [this](int page, inkpages::PointF point) -> LPCWSTR
        {
            if (LPCWSTR link = linkHover->CursorAt(page, point)) return link;
            return page >= 0 && textSelection->TextAt(page, point) ? IDC_IBEAM : nullptr;
        };
        outline = std::make_unique<PdfOutline>(*pane, *view, *links);
        view->pdfLoaded = [this]
        {
            outline->Update();
            textSelection->PdfLoaded();
        };
        surface->tapped = [this](int page, inkpages::PointF point, inkpages::PointerKind)
        {
            textSelection->Clear(); // a click beside it
            links->Tapped(page, point);
        };
        CreateSearch();
        toast = std::make_unique<Toast>(window);
        statusBar = std::make_unique<StatusBar>(window);
        statusBar->pageDoubleClicked = [this] { OnCommand(CmdGoToPage); };
        DragAcceptFiles(window, TRUE); // a .xopp or PDF dropped on the window opens
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
            // Keys while typing (an edit box, a text on the page) are the typing's, not shortcuts.
            bool typing = TypingInEdit() || (surface && surface->EditingText());
            if (typing || !TranslateAcceleratorW(window, menu.Accelerators(), &message))
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
        if (!surface || !toolbar || !pane || !statusBar)
        {
            return;
        }
        RECT rc;
        GetClientRect(window, &rc);
        statusBar->Layout();
        rc.bottom = std::max(0L, rc.bottom - statusBar->Height());
        int top = toolbar->Height();
        MoveWindow(toolbar->Window(), 0, 0, rc.right, top, TRUE);
        // 280 DIP, but never narrower than the tabs need (one row, no scroll arrows - with the font they really have).
        int paneWidth = settings.paneOpen
            ? std::max(MulDiv(PaneWidthDip, static_cast<int>(GetDpiForWindow(window)), 96), pane->TabsWidth()) : 0;
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
        UpdatePagesPane();
    }

    void MainWindow::CreatePagesPane()
    {
        thumbnails = std::make_unique<PageThumbnails>(*view, *surface); // the pane sets their size
        pagesPane = std::make_unique<PagesPane>(pane->Window(), *thumbnails);
        pane->SetPagesContent(pagesPane->Window());
        thumbnails->updated = [this](int index) { pagesPane->Redraw(index); };
        view->shown = [this]
        {
            if (outline) outline->Update(); // the new document's PDF is not loaded yet: the tab goes until it is
            if (search) search->DocumentShown();
            textSelection->DocumentShown();
            thumbnails->Reset();
            pagesPane->SetCount(view->PageCount());
            pagesPane->Select(surface->PageInView().value_or(0));
            navigation->DocumentShown();
            UpdateStatus();
        };
        view->pagesEdited = [this](std::set<int> const& pages) { thumbnails->Edited(pages); };
        view->viewMoved = [this]
        {
            pagesPane->Select(surface->PageInView().value_or(0));
            UpdateStatus();
        };
        pagesPane->goTo = [this](int index) { navigation->Jump([this, index] { surface->ScrollToPage(index); }); };
        pagesPane->insertBlank = [this](int index) { pageCommands->InsertBlankAt(index); };
        pagesPane->insertFromFile = [this](int index) { insertCommand->Insert(index); };
        pagesPane->move = [this](int from, int to)
        {
            pageCommands->Move(from, to);
            pagesPane->Select(to); // the selection follows the page (the view does already)
        };
        pagesPane->remove = [this](int index) { pageCommands->DeleteAt(index); };
    }

    void MainWindow::CreateSearch()
    {
        search = std::make_unique<DocumentSearch>(*view);
        searchPane = std::make_unique<SearchPane>(pane->Window());
        pane->SetSearchContent(searchPane->Window());
        searchPane->searchRequested = [this](std::wstring const& query) { search->Start(query); };
        searchPane->next = [this] { search->Next(); };
        searchPane->previous = [this] { search->Previous(); };
        searchPane->chosen = [this](size_t index)
        {
            navigation->Remember(); // a hit picked from the list is a jump (F3 steps are not)
            search->Show(index);
        };
        searchPane->leave = [this] { SetFocus(surface->Window()); };
        search->changed = [this] { searchPane->Show(search->Hits(), search->Current(), search->Running(), search->Query()); };
        // The hit into view, scrolled only as far as needed (all its lines).
        search->shown = [this](search::SearchHit const& hit)
        {
            if (hit.rects.empty()) { surface->ScrollToPage(hit.page); return; }
            Rect all = hit.rects.front();
            for (auto const& r : hit.rects)
            {
                double left = std::min(all.Left(), r.Left()), top = std::min(all.Top(), r.Top());
                all = { left, top, std::max(all.Right(), r.Right()) - left, std::max(all.Bottom(), r.Bottom()) - top };
            }
            surface->RevealRect(hit.page, { static_cast<float>(all.Left()), static_cast<float>(all.Top()),
                static_cast<float>(all.Right()), static_cast<float>(all.Bottom()) });
        };
    }

    void MainWindow::ShowSearch()
    {
        if (!settings.paneOpen) SetPaneOpen(true);
        pane->SetTab(SidePane::SearchTab);
        settings.paneTab = SidePane::SearchTab;
        SaveSettings();
        UpdatePagesPane();
        searchPane->FocusField();
    }

    void MainWindow::UpdatePagesPane()
    {
        if (thumbnails && pane) thumbnails->SetActive(settings.paneOpen && pane->Tab() == SidePane::PagesTab);
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
        bool text = textSelection->HasSelection(); // read-only: copied, never cut or deleted
        menu.Enable(CmdUndo, surface->CanUndo());
        menu.Enable(CmdRedo, surface->CanRedo());
        bool tree = pane->Explorer().HasFocus(); // Strg+X/C, Entf act on the tree's files then
        for (int command : { CmdCut, CmdCopy, CmdDuplicate, CmdDelete })
        {
            menu.Enable(command, selection || (tree && command != CmdDuplicate) || (text && command == CmdCopy && !tree));
        }
        menu.Enable(CmdClearSelection, selection || text);
        menu.Enable(CmdSync, !sync->IsSyncing());
        menu.Enable(CmdMovePageUp, pageCommands->CanMoveCurrent(-1));
        menu.Enable(CmdMovePageDown, pageCommands->CanMoveCurrent(1));
        menu.Enable(CmdBack, navigation->CanGoBack());
        menu.Enable(CmdForward, navigation->CanGoForward());
    }

    void MainWindow::UpdateStatus()
    {
        if (!statusBar || !view) return;
        statusBar->Show(surface->PageInView().value_or(-1), view->PageCount(), navigation->ZoomPercent());
    }

    void MainWindow::RememberPlace()
    {
        auto key = session ? session->PlaceKey() : std::nullopt;
        auto place = surface->CurrentPlace();
        if (!key || !place) return;
        settings.documentPositions[key->native()] = { place->page, place->point.x, place->point.y, place->zoom };
    }

    void MainWindow::ShowPageMenu(LPARAM lp)
    {
        RECT area;
        GetWindowRect(surface->Window(), &area);
        POINT center{ (area.left + area.right) / 2, (area.top + area.bottom) / 2 }; // a keyboard request
        int command = PageContextMenu::Show(window, { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, center,
            { surface->HasSelection(), textSelection->HasSelection(), navigation->CanGoBack(), navigation->CanGoForward() });
        if (command) OnCommand(command);
    }

    void MainWindow::OpenDropped(HDROP drop)
    {
        size_t others = 0;
        auto documents = FileDrop::Documents(drop, others);
        if (documents.empty())
        {
            Message(L"Nur Xournal++-Dokumente (.xopp), MrWriter-Dokumente (.moj) und PDF-Dateien können geöffnet werden.",
                ToastKind::Warning);
            return;
        }
        if (documents.size() + others > 1)
        {
            Message(std::format(L"Nur „{}“ wird geöffnet - eine Datei auf einmal.", documents.front().filename().native()),
                ToastKind::Info);
        }
        SetForegroundWindow(window);
        session->Open(documents.front());
    }

    void MainWindow::OpenRecent(int index)
    {
        if (index < 0 || index >= static_cast<int>(settings.recentFiles.size())) return;
        session->Open(settings.recentFiles[index]);
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
        sync->finished = [this](bool succeeded) { remoteFetch->SyncEnded(succeeded); };
        remoteFetch = std::make_unique<RemoteFetch>();
        remoteFetch->fetched = [this] { projectExplorer->Refresh(); }; // the fetch wrote only inside .git (not watched)
        session->refusedWhileSyncing = [this](std::wstring const& what) { return sync->RefusedWhileSyncing(what); };
        session->saveDeferredWhileSyncing = [this] { return sync->DeferSave(); };
        session->folderForNew = [this]() -> std::optional<std::filesystem::path>
        {
            if (auto selected = pane->Explorer().SelectedFolder()) return selected;
            return projectExplorer ? projectExplorer->Folder() : std::nullopt;
        };

        projectExplorer = std::make_unique<ProjectExplorer>(pane->Explorer());
        projectExplorer->refreshed = [this]
        {
            pane->Tasks().Reload();
            UpdateSyncButton();
        };
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
        remoteFetch->SetFolder(folder);
    }

    void MainWindow::UpdateSyncButton()
    {
        git::SyncStatus status = projectExplorer->SyncStatus();
        status.remoteKnown = remoteFetch->RemoteKnown();
        pane->SetSyncStatus(status.ButtonText(), status.Details());
    }

    void MainWindow::OnSyncStateChanged()
    {
        pane->SetSyncing(sync->IsSyncing());
        if (sync->IsSyncing())
        {
            remoteFetch->Cancel(); // the sync fetches itself; it would wait behind a slow background fetch
        }
        else
        {
            projectExplorer->Refresh();
        }
    }

    void MainWindow::OnCommand(int command)
    {
        // A text being typed is part of the document before any command acts on it (save, export, leave, ...).
        if (surface && surface->EditingText()) surface->EndTextEditing();
        switch (command)
        {
        case CmdNew: session->New(); break;
        case CmdOpen: session->Open(); break;
        case CmdSave: session->Save(); break;
        case CmdSaveAs: session->SaveAs(); break;
        case CmdExportPdf: exportCommand->Export(session->Document()); break;
        case CmdInsertPage: pageCommands->InsertAfterCurrent(); break;
        case CmdInsertFromFile: insertCommand->Insert(surface->PageInView().value_or(0)); break;
        case CmdImportPdf: importCommand->Import(); break;
        case CmdDeletePage: pageCommands->DeleteCurrent(); break;
        case CmdMovePageUp: pageCommands->MoveCurrent(-1); break;
        case CmdMovePageDown: pageCommands->MoveCurrent(1); break;
        case CmdOpenFolder: OpenFolder(); break;
        case CmdSync: sync->Sync(projectExplorer->Folder()); break;
        case CmdExit: PostMessageW(window, WM_CLOSE, 0, 0); break;
        case CmdUndo: surface->Undo(); break;
        case CmdRedo: surface->Redo(); break;
        // Strg+X/C/V in the explorer tree are its files'.
        case CmdCut:
            if (pane->Explorer().HasFocus()) pane->Explorer().RequestForSelection(ExplorerCommand::Cut);
            else surface->Cut();
            break;
        case CmdCopy:
            if (pane->Explorer().HasFocus()) pane->Explorer().RequestForSelection(ExplorerCommand::Copy);
            else if (textSelection->HasSelection()) CopyPdfText();
            else surface->Copy();
            break;
        case CmdPaste:
            if (pane->Explorer().HasFocus()) pane->Explorer().RequestForSelection(ExplorerCommand::Paste);
            else surface->Paste();
            break;
        case CmdDuplicate: surface->Duplicate(); break;
        case CmdDelete:
            if (pane->Explorer().HasFocus()) pane->Explorer().RequestForSelection(ExplorerCommand::Delete); // Entf in the tree
            else surface->DeleteSelection();
            break;
        case CmdSelectAll: surface->SelectAll(); break;
        case CmdClearSelection:
            surface->ClearSelection();
            textSelection->Clear();
            break;
        case CmdTogglePane: SetPaneOpen(!settings.paneOpen); SaveSettings(); break;
        case CmdFind: ShowSearch(); break;
        case CmdFindNext: if (search->Hits().empty()) ShowSearch(); else search->Next(); break;
        case CmdFindPrevious: if (search->Hits().empty()) ShowSearch(); else search->Previous(); break;
        case CmdFitWidth: surface->FitWidth(); break;
        case CmdFitPage: surface->FitPage(); break;
        case CmdActualSize: navigation->ActualSize(); break;
        case CmdZoomIn: navigation->ZoomIn(); break;
        case CmdZoomOut: navigation->ZoomOut(); break;
        case CmdGoToPage: navigation->GoToPage(); break;
        case CmdBack: navigation->Back(); break;
        case CmdForward: navigation->Forward(); break;
        default:
            if (command >= CmdRecentFirst && command <= CmdRecentLast)
            {
                OpenRecent(command - CmdRecentFirst);
                break;
            }
            Log(std::format("command {} is not available yet", command)); // disabled in the menu; an accelerator may still come
            break;
        }
    }

    void MainWindow::CopyPdfText()
    {
        try
        {
            textSelection->Copy();
        }
        catch (...)
        {
            LogException("clipboard: copy the PDF's text");
            Message(std::format(L"Kopieren fehlgeschlagen: {}", ErrorText()), ToastKind::Error);
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
        // Ending the process mid fetch/rebase/push could leave the repository mid-rebase or locked: the window waits.
        if (sync->RefusedWhileSyncing(L"Schließen")) return false;
        if (!session->ConfirmLeave())
        {
            return false;
        }
        recovery->End(); // saved or discarded: nothing to recover
        RememberPlace();
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
        case WM_NOTIFY:
            if (statusBar && statusBar->OnNotify(*reinterpret_cast<NMHDR const*>(lp))) return 0;
            break;
        case WM_CONTEXTMENU:
            // The pages' menu (InkPages leaves the right click to the parent); other children have their own.
            if (surface && reinterpret_cast<HWND>(wp) == surface->Window())
            {
                ShowPageMenu(lp);
                return 0;
            }
            break;
        case WM_APPCOMMAND:
            // The mouse's back/forward buttons (Windows makes them from WM_XBUTTONUP) and the keyboard's browser keys.
            switch (GET_APPCOMMAND_LPARAM(lp))
            {
            case APPCOMMAND_BROWSER_BACKWARD: OnCommand(CmdBack); return TRUE;
            case APPCOMMAND_BROWSER_FORWARD: OnCommand(CmdForward); return TRUE;
            }
            break;
        case WM_DROPFILES:
            OpenDropped(reinterpret_cast<HDROP>(wp));
            return 0;
        case WM_SETFOCUS:
            // The keyboard goes back where it was (the tree, the search box), else to the pages (scroll keys).
            if (surface)
            {
                HWND target = focusBefore && IsWindow(focusBefore) && IsChild(window, focusBefore) && IsWindowVisible(focusBefore)
                    ? focusBefore : surface->Window();
                SetFocus(target);
                return 0;
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
            {
                focusBefore = GetFocus();
                if (recovery) recovery->Deactivated(); // maybe for good (shutdown, log off): the copy now
            }
            else if (projectExplorer)
            {
                projectExplorer->Refresh(); // files may have changed in another app
                if (remoteFetch) remoteFetch->FetchIfDue(); // and the server (at most every few minutes)
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
            remoteFetch.reset();
            projectExplorer.reset();
            sync.reset();
            // The feature classes that hold the surface go before it (the view takes its callbacks back).
            surface->tapped = nullptr;
            surface->mouseClaims = nullptr;
            surface->mouseGesture = nullptr;
            surface->selectionChanged = nullptr;
            surface->textEditingChanged = nullptr;
            surface->cursorAt = nullptr;
            linkHover.reset();
            view->pdfLoaded = nullptr;
            search.reset();
            searchPane.reset();
            outline.reset();
            links.reset();
            pagesPane.reset();
            thumbnails.reset();
            importCommand.reset();
            insertCommand.reset();
            pageCommands.reset();
            exportCommand.reset();
            session.reset();
            view.reset();
            textSelection.reset(); // after the view: its handlers call it
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
