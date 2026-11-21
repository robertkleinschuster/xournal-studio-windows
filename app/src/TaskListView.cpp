#include "TaskListView.h"
#include "DarkColors.h"

#include "ChildControls.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <format>

namespace xournal::app
{
    namespace
    {
        enum GroupId : int { Overdue = 1, Open, Done };
        constexpr wchar_t DueColumnTitle[] = L"Fällig";
        constexpr wchar_t const* ColumnTitles[] = { L"Aufgabe", DueColumnTitle };

        LRESULT CALLBACK DrawTaskHeader(HWND list, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
        {
            if (message == WM_NOTIFY && DarkColors::Enabled(list))
            {
                auto const& note = *reinterpret_cast<NMHDR const*>(lp);
                if (note.code == NM_CUSTOMDRAW && note.hwndFrom == ListView_GetHeader(list))
                {
                    auto const& draw = reinterpret_cast<NMCUSTOMDRAW const&>(note);
                    if (draw.dwDrawStage == CDDS_PREPAINT)
                    {
                        if (DarkColors::PaintBackground(note.hwndFrom, draw.hdc)) return CDRF_NOTIFYITEMDRAW;
                        LogError("FillRect (task header background)");
                    }
                    if (draw.dwDrawStage == CDDS_ITEMPREPAINT && draw.dwItemSpec < ARRAYSIZE(ColumnTitles))
                    {
                        auto color = draw.uItemState & CDIS_HOT ? DarkColors::Selected : DarkColors::Input;
                        if (FillRect(draw.hdc, &draw.rc, DarkColors::ControlBrush(draw.hdc, color)))
                        {
                            RECT label = draw.rc;
                            label.left += MulDiv(6, GetDpiForWindow(note.hwndFrom), 96);
                            SetBkMode(draw.hdc, TRANSPARENT);
                            DrawTextW(draw.hdc, ColumnTitles[draw.dwItemSpec], -1, &label,
                                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                            return CDRF_SKIPDEFAULT;
                        }
                        LogError("FillRect (task header item)");
                    }
                }
            }
            return DefSubclassProc(list, message, wp, lp);
        }

        int GroupOf(TaskRow const& row) { return row.task.done ? Done : row.overdue ? Overdue : Open; }

        std::wstring Shown(TaskRow const& row)
        {
            std::wstring text = row.task.priority ? std::format(L"({}) {}", *row.task.priority, row.title) : row.title;
            return row.linkText.empty() ? text : text + L" · " + row.linkText;
        }

        // Everything about the row, for the info tip.
        std::wstring Details(TaskRow const& row)
        {
            std::wstring text = Shown(row);
            if (!row.dueText.empty()) text += std::format(L"\n{} {}", row.overdue ? L"Überfällig seit" : L"Fällig", row.dueText);
            if (row.task.done && row.task.completed)
            {
                text += std::format(L"\nErledigt am {:02}.{:02}.{}", row.task.completed->day, row.task.completed->month,
                    row.task.completed->year);
            }
            if (row.link) text += L"\nDie verknüpfte Seite öffnet das Kontextmenü.";
            return text;
        }
    }

    TaskListView::TaskListView(HWND parent, int id)
    {
        list = ChildControls::Create(parent, WC_LISTVIEWW, L"Aufgaben", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | LVS_EDITLABELS, id);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP |
            LVS_EX_DOUBLEBUFFER);
        HRESULT hr = SetWindowTheme(list, L"Explorer", nullptr); // File Explorer's look: selection, group headers
        if (FAILED(hr))
        {
            LogError(std::format("tasks: list theme not set: {}", DescribeHr(hr)));
        }
        if (!SetWindowSubclass(list, DrawTaskHeader, 1, 0))
            ThrowHr("SetWindowSubclass (task header)", E_FAIL);
        for (int i = 0; i < 2; ++i)
        {
            LVCOLUMNW column{ LVCF_TEXT | LVCF_WIDTH };
            column.pszText = const_cast<wchar_t*>(ColumnTitles[i]);
            column.cx = 100;
            if (ListView_InsertColumn(list, i, &column) < 0) ThrowLastError("ListView_InsertColumn (tasks)");
        }
        if (ListView_EnableGroupView(list, TRUE) < 0) ThrowLastError("ListView_EnableGroupView (tasks)");
    }


    void TaskListView::Layout(int left, int top, int width, int height, int)
    {
        if (!MoveWindow(list, left, top, width, std::max(0, height), TRUE))
            ThrowLastError("MoveWindow (task list)");
        FitColumns();
    }

    // Fällig as wide as its widest text (or its header), Aufgabe the rest of the visible width.
    void TaskListView::FitColumns()
    {
        UINT dpi = GetDpiForWindow(list);
        if (!dpi) ThrowHr("GetDpiForWindow (task list)", E_FAIL);
        if (!dueWidth || columnsDpi != dpi)
        {
            if (!ListView_SetColumnWidth(list, 1, LVSCW_AUTOSIZE))
                ThrowHr("LVM_SETCOLUMNWIDTH (measure task dates)", E_FAIL);
            int header = ListView_GetStringWidth(list, DueColumnTitle) + MulDiv(16, static_cast<int>(dpi), 96);
            dueWidth = std::max(header, ListView_GetColumnWidth(list, 1));
            columnsDpi = dpi;
        }
        if (ListView_GetColumnWidth(list, 1) != dueWidth && !ListView_SetColumnWidth(list, 1, dueWidth))
            ThrowHr("LVM_SETCOLUMNWIDTH (task dates)", E_FAIL);
        RECT client{};
        if (!GetClientRect(list, &client)) ThrowLastError("GetClientRect (task list)");
        int titleWidth = std::max(MulDiv(60, static_cast<int>(dpi), 96), static_cast<int>(client.right) - dueWidth);
        if (ListView_GetColumnWidth(list, 0) != titleWidth && !ListView_SetColumnWidth(list, 0, titleWidth))
            ThrowHr("LVM_SETCOLUMNWIDTH (task titles)", E_FAIL);
    }

    void TaskListView::Show(TaskRows const& shown) try
    {
        filling = true;
        dueWidth = 0;
        ChildControls::BatchRedraw redraw(list);
        if (!ListView_DeleteAllItems(list)) ThrowHr("LVM_DELETEALLITEMS (tasks)", E_FAIL);
        rows = shown.rows;
        ListView_RemoveAllGroups(list);
        // Only groups that have tasks (in this order).
        for (int group : { Overdue, Open, Done })
        {
            auto count = std::ranges::count_if(rows, [group](TaskRow const& r) { return GroupOf(r) == group; });
            if (count == 0) continue;
            std::wstring header = std::format(L"{} ({})", group == Overdue ? L"Überfällig" : group == Open ? L"Offen" : L"Erledigt", count);
            LVGROUP g{ sizeof(g) };
            g.mask = LVGF_HEADER | LVGF_GROUPID;
            g.pszHeader = header.data();
            g.iGroupId = group;
            if (ListView_InsertGroup(list, -1, &g) < 0)
                ThrowHr("LVM_INSERTGROUP (tasks)", E_FAIL);
        }
        for (size_t i = 0; i < rows.size(); i++)
        {
            TaskRow const& row = rows[i];
            std::wstring title = Shown(row);
            LVITEMW item{ LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID };
            item.iItem = static_cast<int>(i);
            item.pszText = title.data();
            item.lParam = static_cast<LPARAM>(i);
            item.iGroupId = GroupOf(row);
            int index = ListView_InsertItem(list, &item);
            if (index < 0)
                ThrowHr("LVM_INSERTITEM (tasks)", E_FAIL);
            ListView_SetItemText(list, index, 1, const_cast<wchar_t*>(row.dueText.c_str()));
            ListView_SetCheckState(list, index, row.task.done);
        }
        FitColumns();
        filling = false;
    }
    catch (...)
    {
        filling = false;
        throw;
    }

    TaskRow const* TaskListView::RowOfItem(int item) const
    {
        if (item < 0) return nullptr;
        LVITEMW lv{ LVIF_PARAM, item };
        if (!ListView_GetItem(list, &lv)) return nullptr;
        size_t index = static_cast<size_t>(lv.lParam);
        return index < rows.size() ? &rows[index] : nullptr;
    }

    LRESULT TaskListView::OnCustomDraw(NMLVCUSTOMDRAW& draw)
    {
        if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw.dwItemType != LVCDI_ITEM) return CDRF_DODEFAULT;
        if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
        if (draw.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
        {
            size_t index = static_cast<size_t>(draw.nmcd.lItemlParam);
            if (index < rows.size())
            {
                TaskRow const& row = rows[index];
                bool dueColumn = draw.iSubItem == 1;
                if (DarkColors::Enabled(list))
                {
                    draw.clrText = row.task.done ? RGB(170, 172, 178)
                        : dueColumn && row.overdue ? RGB(255, 153, 146)
                        : dueColumn && row.dueSoon ? RGB(153, 198, 255)
                        : DarkColors::Text;
                    return CDRF_DODEFAULT;
                }
                draw.clrText = row.task.done ? GetSysColor(COLOR_GRAYTEXT)
                    : dueColumn && row.overdue ? RGB(0xC4, 0x2B, 0x1C)
                    : dueColumn && row.dueSoon ? GetSysColor(COLOR_HOTLIGHT)
                    : GetSysColor(COLOR_WINDOWTEXT);
            }
        }
        return CDRF_DODEFAULT;
    }

    LRESULT TaskListView::OnNotify(NMHDR const& note)
    {
        switch (note.code)
        {
        case LVN_ITEMCHANGED:
        {
            auto const& change = reinterpret_cast<NMLISTVIEW const&>(note);
            bool checkChanged = (change.uChanged & LVIF_STATE) && ((change.uNewState ^ change.uOldState) & LVIS_STATEIMAGEMASK) &&
                (change.uOldState & LVIS_STATEIMAGEMASK); // 0: the item was just inserted
            size_t index = static_cast<size_t>(change.lParam);
            if (!filling && checkChanged && index < rows.size() && toggled)
            {
                // Not inside the list's own notification: the change refills the list.
                UiQueue::Post("tasks: complete", [this, copy = rows[index]] { toggled(copy); });
            }
            return 0;
        }
        case LVN_KEYDOWN:
            // F2 renames, as in File Explorer: the ListView leaves the key to its owner (it edits labels by itself only
            // on a click on the selected item); LVM_EDITLABEL on the focused, selected item.
            if (reinterpret_cast<NMLVKEYDOWN const&>(note).wVKey == VK_F2)
            {
                int item = ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
                Log(std::format("tasks: F2 on row {}", item));
                if (item >= 0 && !ListView_EditLabel(list, item)) LogError(std::format("tasks: LVM_EDITLABEL of row {} started no edit", item));
            }
            return 0;
        case LVN_BEGINLABELEDITW:
        {
            auto const& info = reinterpret_cast<NMLVDISPINFOW const&>(note);
            TaskRow const* row = RowOfItem(info.item.iItem);
            if (!row || !renamed)
            {
                Log(std::format("tasks: no in-place edit of row {} ({})", info.item.iItem, row ? "no handler" : "no task"));
                return TRUE;
            }
            // The edit starts with the text as written (priority, +projects, @contexts, other tags), not the shown title.
            HWND edit = ListView_GetEditControl(list);
            if (!edit)
            {
                LogError("tasks: in-place edit without the list's edit control (not started)");
                return TRUE;
            }
            if (!SetWindowTextW(edit, TodoTxt::EditText(row->task).c_str())) ThrowLastError("SetWindowText (task edit)");
            Edit_SetSel(edit, 0, -1);
            Log(std::format("tasks: editing row {} in place", info.item.iItem));
            return FALSE;
        }
        case LVN_ENDLABELEDITW:
        {
            auto const& info = reinterpret_cast<NMLVDISPINFOW const&>(note);
            TaskRow const* row = RowOfItem(info.item.iItem);
            if (info.item.pszText && row && renamed)
            {
                UiQueue::Post("tasks: edit in place", [this, copy = *row, text = std::wstring(info.item.pszText)] { renamed(copy, text); });
            }
            return FALSE; // the list is refilled from the file
        }
        case LVN_GETINFOTIPW:
        {
            auto& tip = const_cast<NMLVGETINFOTIPW&>(reinterpret_cast<NMLVGETINFOTIPW const&>(note));
            if (TaskRow const* row = RowOfItem(tip.iItem); row && tip.pszText && tip.cchTextMax > 0)
            {
                wcsncpy_s(tip.pszText, static_cast<size_t>(tip.cchTextMax), Details(*row).c_str(), _TRUNCATE);
            }
            return 0;
        }
        case LVN_GETEMPTYMARKUP:
        {
            auto& empty = const_cast<NMLVEMPTYMARKUP&>(reinterpret_cast<NMLVEMPTYMARKUP const&>(note));
            empty.dwFlags = EMF_CENTERED;
            wcsncpy_s(empty.szMarkup, L"Keine offenen Aufgaben.", _TRUNCATE);
            return TRUE;
        }
        case NM_DBLCLK:
            // A double-click (mouse or pen) on a row edits the task (the dialog: text, due date, link) - after the
            // list's own notification, since the change refills the list.
            if (TaskRow const* row = RowOfItem(reinterpret_cast<NMITEMACTIVATE const&>(note).iItem); row && editRequested)
            {
                Log(std::format("tasks: double-click on row {}", reinterpret_cast<NMITEMACTIVATE const&>(note).iItem));
                UiQueue::Post("tasks: edit", [this, copy = *row] { editRequested(copy); });
            }
            return 0;
        case NM_RCLICK:
        {
            auto const& activate = reinterpret_cast<NMITEMACTIVATE const&>(note);
            if (TaskRow const* row = RowOfItem(activate.iItem); row && menuRequested)
            {
                POINT screen = activate.ptAction;
                ClientToScreen(list, &screen);
                menuRequested(*row, screen);
            }
            return TRUE;
        }
        case NM_CUSTOMDRAW:
            return OnCustomDraw(const_cast<NMLVCUSTOMDRAW&>(reinterpret_cast<NMLVCUSTOMDRAW const&>(note)));
        }
        return 0;
    }
}
