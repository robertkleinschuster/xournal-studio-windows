#include "TasksPane.h"

#include "Questions.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioTasksPane";
        enum ControlId : int { NewTaskId = 1, DueId, LinkId, AddId, ListId, ShowDoneId, CreateId };
        enum MenuId : UINT { MenuOpenLink = 1, MenuDelete };

        HWND Control(HWND parent, wchar_t const* type, wchar_t const* text, DWORD style, int id, DWORD exStyle = 0)
        {
            HWND control = CreateWindowExW(exStyle, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            if (!control)
            {
                ThrowLastError(std::format("CreateWindowEx(tasks pane control {})", id));
            }
            return control;
        }
    }

    TasksPane::TasksPane(HWND parent, Messages show) : messages(std::move(show))
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        if (!GetClassInfoExW(instance, ClassName, &wc))
        {
            wc = { sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = ClassName;
            if (!RegisterClassExW(&wc))
            {
                ThrowLastError("RegisterClassEx(tasks pane)");
            }
        }
        window = CreateWindowExW(WS_EX_CONTROLPARENT, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
            instance, this);
        if (!window)
        {
            ThrowLastError("CreateWindowEx(tasks pane)");
        }
        CreateControls();
        Reload();
    }

    TasksPane::~TasksPane()
    {
        if (window && IsWindow(window))
        {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            RemoveWindowSubclass(newTask, EditProc, 0);
        }
    }

    void TasksPane::CreateControls()
    {
        header = Control(window, WC_STATICW, L"Aufgaben", SS_LEFT | SS_ENDELLIPSIS, 0);
        newTask = Control(window, WC_EDITW, L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, NewTaskId);
        Edit_SetCueBannerTextFocused(newTask, L"Neue Aufgabe (Eingabe fügt hinzu)", TRUE);
        if (!SetWindowSubclass(newTask, EditProc, 0, reinterpret_cast<DWORD_PTR>(this)))
        {
            ThrowLastError("SetWindowSubclass(new task)");
        }
        due = Control(window, DATETIMEPICK_CLASSW, L"", WS_TABSTOP | DTS_SHORTDATEFORMAT | DTS_SHOWNONE, DueId);
        DateTime_SetSystemtime(due, GDT_NONE, nullptr); // no due date until one is chosen
        linkPage = Control(window, WC_BUTTONW, L"Mit dieser Seite verknüpfen", WS_TABSTOP | BS_AUTOCHECKBOX, LinkId);
        addButton = Control(window, WC_BUTTONW, L"Hinzufügen", WS_TABSTOP | BS_PUSHBUTTON, AddId);
        list = Control(window, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS, ListId);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_DOUBLEBUFFER);
        HRESULT hr = SetWindowTheme(list, L"Explorer", nullptr);
        if (FAILED(hr))
        {
            LogError(std::format("tasks: list theme not set: {}", DescribeHr(hr)));
        }
        LVCOLUMNW column{ LVCF_WIDTH };
        column.cx = 160;
        ListView_InsertColumn(list, 0, &column);
        column.cx = 100;
        ListView_InsertColumn(list, 1, &column);
        showDoneBox = Control(window, WC_BUTTONW, L"Erledigte anzeigen", WS_TABSTOP | BS_AUTOCHECKBOX, ShowDoneId);
        noFile = Control(window, WC_STATICW, L"", SS_LEFT, 0);
        createButton = Control(window, WC_BUTTONW, L"todo.txt anlegen", WS_TABSTOP | BS_PUSHBUTTON, CreateId);

        NONCLIENTMETRICSW metrics{ sizeof(metrics) };
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, GetDpiForWindow(window)))
        {
            static HFONT font = CreateFontIndirectW(&metrics.lfMessageFont); // one per process, kept until exit
            for (HWND control : { header, newTask, due, linkPage, addButton, list, showDoneBox, noFile, createButton })
            {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        }
        else
        {
            LogError(std::format("tasks: message font: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
        }
    }

    void TasksPane::Layout()
    {
        RECT rc;
        GetClientRect(window, &rc);
        int dpi = static_cast<int>(GetDpiForWindow(window));
        auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        int margin = px(8), width = rc.right - 2 * margin, y = margin;
        bool exists = file && file->Exists();
        MoveWindow(header, margin, y, width, px(20), TRUE);
        y += px(24);
        if (!exists)
        {
            MoveWindow(noFile, margin, y, width, px(40), TRUE);
            MoveWindow(createButton, margin, y + px(44), px(140), px(28), TRUE);
            return;
        }
        MoveWindow(newTask, margin, y, width, px(26), TRUE);
        y += px(30);
        int addWidth = px(90);
        MoveWindow(due, margin, y, width - addWidth - margin, px(26), TRUE);
        MoveWindow(addButton, rc.right - margin - addWidth, y, addWidth, px(26), TRUE);
        y += px(30);
        MoveWindow(linkPage, margin, y, width, px(22), TRUE);
        y += px(28);
        int bottom = rc.bottom - margin - px(22);
        MoveWindow(list, margin, y, width, std::max(0, bottom - y - px(4)), TRUE);
        ListView_SetColumnWidth(list, 1, px(110));
        ListView_SetColumnWidth(list, 0, std::max(px(60), width - px(110) - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) - px(4)));
        MoveWindow(showDoneBox, margin, bottom, width, px(22), TRUE);
    }

    void TasksPane::SetFolder(std::optional<std::filesystem::path> const& folder)
    {
        file.reset();
        if (folder)
        {
            file.emplace(*folder / L"todo.txt");
        }
        Reload();
    }

    void TasksPane::SetShowsDone(bool value)
    {
        showDone = value;
        Button_SetCheck(showDoneBox, value ? BST_CHECKED : BST_UNCHECKED);
        Reload();
    }

    void TasksPane::Reload()
    {
        bool exists = false;
        rows = {};
        try
        {
            exists = file && file->Exists();
            if (exists)
            {
                rows = TaskRows::From(file->ReadLines(), file->Path(), showDone);
            }
        }
        catch (...)
        {
            LogException(std::format("tasks: read {}", file ? ToUtf8(file->Path().native()) : ""));
            messages(L"todo.txt konnte nicht gelesen werden (Einzelheiten in errors.log).", ToastKind::Error);
        }
        for (HWND control : { newTask, due, linkPage, addButton, list, showDoneBox })
        {
            ShowWindow(control, exists ? SW_SHOW : SW_HIDE);
        }
        ShowWindow(noFile, exists ? SW_HIDE : SW_SHOW);
        ShowWindow(createButton, !exists && file ? SW_SHOW : SW_HIDE);
        SetWindowTextW(noFile, file ? L"Im Projektordner gibt es keine todo.txt." : L"Kein Ordner geöffnet (Datei → Ordner öffnen).");
        SetWindowTextW(header, !exists ? L"Aufgaben"
            : rows.openCount == 1 ? L"todo.txt – 1 offen" : std::format(L"todo.txt – {} offen", rows.openCount).c_str());
        bool canLink = currentPage && currentPage().first.has_value();
        EnableWindow(linkPage, canLink);
        if (!canLink) Button_SetCheck(linkPage, BST_UNCHECKED);
        Fill();
        Layout();
    }

    void TasksPane::Fill()
    {
        filling = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (size_t i = 0; i < rows.rows.size(); i++)
        {
            TaskRow const& row = rows.rows[i];
            std::wstring title = row.linkText.empty() ? row.title : row.title + L" · " + row.linkText;
            LVITEMW item{ LVIF_TEXT | LVIF_PARAM };
            item.iItem = static_cast<int>(i);
            item.pszText = title.data();
            item.lParam = static_cast<LPARAM>(i);
            int index = ListView_InsertItem(list, &item);
            if (index < 0)
            {
                ThrowLastError("ListView_InsertItem(task)");
            }
            ListView_SetItemText(list, index, 1, const_cast<wchar_t*>(row.dueText.c_str()));
            ListView_SetCheckState(list, index, row.task.done);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        filling = false;
    }

    bool TasksPane::Change(char const* what, std::function<bool(tasks::TaskFile&)> const& change)
    {
        if (!file)
        {
            return false;
        }
        if (refusedWhileSyncing && refusedWhileSyncing())
        {
            Reload(); // e.g. the check box back to what the file says
            return false;
        }
        bool written = false;
        try
        {
            written = change(*file);
            if (!written)
            {
                messages(L"Die Aufgabe wurde inzwischen geändert (andere App oder Synchronisierung); die Liste ist neu geladen.",
                    ToastKind::Warning);
            }
        }
        catch (...)
        {
            LogException(std::format("tasks: {} in {}", what, ToUtf8(file->Path().native())));
            messages(L"todo.txt konnte nicht geschrieben werden (Einzelheiten in errors.log).", ToastKind::Error);
        }
        Reload();
        return written;
    }

    void TasksPane::Add()
    {
        int length = GetWindowTextLengthW(newTask);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(newTask, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        if (!file || Trim(text).empty())
        {
            return;
        }
        std::optional<Date> dueDate;
        SYSTEMTIME chosen{};
        if (DateTime_GetSystemtime(due, &chosen) == GDT_VALID)
        {
            dueDate = Date{ chosen.wYear, chosen.wMonth, chosen.wDay };
        }
        TodoTask task = TodoTxt::Create(Trim(text), Today(), dueDate);
        if (Button_GetCheck(linkPage) == BST_CHECKED && currentPage)
        {
            auto [document, page] = currentPage();
            if (document)
            {
                task.description = TodoTxt::SetTag(task.description, L"doc", TodoTxt::DocumentLink(file->Path(), *document, page));
            }
        }
        std::wstring line = task.ToString();
        if (!Change("add task", [&](tasks::TaskFile& f) { f.Append(line); return true; }))
        {
            return; // what was typed stays
        }
        SetWindowTextW(newTask, L"");
        DateTime_SetSystemtime(due, GDT_NONE, nullptr);
        Button_SetCheck(linkPage, BST_UNCHECKED);
    }

    void TasksPane::Toggle(TaskRow const& row)
    {
        TodoTask changed = row.task.done ? row.task.Reopen() : row.task.Complete(Today());
        Change("complete task", [&](tasks::TaskFile& f) { return f.Replace(row.line, changed.ToString()); });
    }

    void TasksPane::ShowMenu(int index, POINT screen)
    {
        if (index < 0 || static_cast<size_t>(index) >= rows.rows.size())
        {
            return;
        }
        TaskRow row = rows.rows[static_cast<size_t>(index)];
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            ThrowLastError("CreatePopupMenu");
        }
        AppendMenuW(menu, MF_STRING | (row.link ? MF_ENABLED : MF_GRAYED), MenuOpenLink, L"Verknüpfte Seite ö&ffnen");
        AppendMenuW(menu, MF_STRING, MenuDelete, L"&Löschen");
        UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, window, nullptr));
        DestroyMenu(menu);
        if (chosen == MenuOpenLink && row.link && openRequested)
        {
            openRequested(row.link->first, row.link->second);
        }
        else if (chosen == MenuDelete && Questions::Confirm(window, L"Aufgabe löschen?",
            std::format(L"„{}“ wird aus der todo.txt entfernt.", row.title), L"Löschen"))
        {
            Change("delete task", [&](tasks::TaskFile& f) { return f.Remove(row.line); });
        }
    }

    void TasksPane::CreateTaskFile()
    {
        Change("create todo.txt", [](tasks::TaskFile& f)
        {
            if (f.Exists())
            {
                return true; // another app made one meanwhile: shown, never overwritten
            }
            io::Files::WriteReplacing(f.Path(), {});
            return true;
        });
    }

    LRESULT TasksPane::OnNotify(NMHDR const& note)
    {
        if (note.hwndFrom != list)
        {
            return 0;
        }
        switch (note.code)
        {
        case LVN_ITEMCHANGED:
        {
            auto const& change = reinterpret_cast<NMLISTVIEW const&>(note);
            bool checkChanged = (change.uChanged & LVIF_STATE) && ((change.uNewState ^ change.uOldState) & LVIS_STATEIMAGEMASK) &&
                (change.uOldState & LVIS_STATEIMAGEMASK); // 0: the item was just inserted
            if (!filling && checkChanged)
            {
                auto index = static_cast<size_t>(change.lParam);
                if (index < rows.rows.size())
                {
                    // Not inside the list's own notification: the change refills the list.
                    UiQueue::Post("tasks: complete", [this, row = rows.rows[index]] { Toggle(row); });
                }
            }
            return 0;
        }
        case NM_DBLCLK:
        {
            auto const& activate = reinterpret_cast<NMITEMACTIVATE const&>(note);
            if (activate.iItem >= 0 && static_cast<size_t>(activate.iItem) < rows.rows.size())
            {
                auto const& row = rows.rows[static_cast<size_t>(activate.iItem)];
                if (row.link && openRequested)
                {
                    openRequested(row.link->first, row.link->second);
                }
            }
            return 0;
        }
        case NM_RCLICK:
        {
            auto const& activate = reinterpret_cast<NMITEMACTIVATE const&>(note);
            POINT screen = activate.ptAction;
            ClientToScreen(list, &screen);
            ShowMenu(activate.iItem, screen);
            return TRUE;
        }
        case NM_CUSTOMDRAW:
        {
            auto& draw = const_cast<NMLVCUSTOMDRAW&>(reinterpret_cast<NMLVCUSTOMDRAW const&>(note));
            if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
            if (draw.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
            {
                auto index = static_cast<size_t>(draw.nmcd.lItemlParam);
                if (index < rows.rows.size())
                {
                    TaskRow const& row = rows.rows[index];
                    draw.clrText = row.task.done ? GetSysColor(COLOR_GRAYTEXT)
                        : draw.iSubItem == 1 && row.overdue ? RGB(0xC4, 0x2B, 0x1C)
                        : draw.iSubItem == 1 && row.dueSoon ? GetSysColor(COLOR_HOTLIGHT)
                        : draw.iSubItem == 1 ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT);
                }
                return CDRF_DODEFAULT;
            }
            return CDRF_DODEFAULT;
        }
        }
        return 0;
    }

    LRESULT TasksPane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case AddId: Add(); return 0;
            case CreateId: CreateTaskFile(); return 0;
            case ShowDoneId:
                showDone = Button_GetCheck(showDoneBox) == BST_CHECKED;
                Reload();
                if (showsDoneChanged) showsDoneChanged(showDone);
                return 0;
            }
            break;
        case WM_NOTIFY:
            return OnNotify(*reinterpret_cast<NMHDR const*>(lp));
        case WM_CTLCOLORSTATIC:
            // The labels on the pane's window background (not the dialog color).
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK TasksPane::EditProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
    {
        // Enter in the new task's box adds it (a single-line edit would only beep).
        if (message == WM_KEYDOWN && wp == VK_RETURN)
        {
            try
            {
                reinterpret_cast<TasksPane*>(data)->Add();
            }
            catch (...)
            {
                LogException("tasks: add");
            }
            return 0;
        }
        if (message == WM_CHAR && wp == L'\r')
        {
            return 0;
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    }

    LRESULT CALLBACK TasksPane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<TasksPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->window || !self->list)
        {
            return DefWindowProcW(hwnd, message, wp, lp);
        }
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("tasks pane: message 0x{:X}", message));
            return 0;
        }
    }
}
