#include "TaskListView.h"

#include "ChildControls.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <uxtheme.h>

#include <algorithm>
#include <format>

namespace xournal::app
{
    TaskListView::TaskListView(HWND parent, int id)
    {
        list = ChildControls::Create(parent, WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
            LVS_SHOWSELALWAYS, id);
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
    }

    void TaskListView::Layout(int left, int top, int width, int height, int dpi)
    {
        auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        MoveWindow(list, left, top, width, std::max(0, height), TRUE);
        ListView_SetColumnWidth(list, 1, px(130));
        ListView_SetColumnWidth(list, 0, std::max(px(60), width - px(130) - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) - px(4)));
    }

    void TaskListView::Show(TaskRows const& shown)
    {
        rows = shown.rows;
        filling = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (size_t i = 0; i < rows.size(); i++)
        {
            TaskRow const& row = rows[i];
            std::wstring title = row.linkText.empty() ? row.title : row.title + L" · " + row.linkText;
            LVITEMW item{ LVIF_TEXT | LVIF_PARAM };
            item.iItem = static_cast<int>(i);
            item.pszText = title.data();
            item.lParam = static_cast<LPARAM>(i);
            int index = ListView_InsertItem(list, &item);
            if (index < 0)
            {
                filling = false;
                ThrowLastError("ListView_InsertItem(task)");
            }
            ListView_SetItemText(list, index, 1, const_cast<wchar_t*>(row.dueText.c_str()));
            ListView_SetCheckState(list, index, row.task.done);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        filling = false;
    }

    TaskRow const* TaskListView::RowAt(int index) const
    {
        return index >= 0 && static_cast<size_t>(index) < rows.size() ? &rows[static_cast<size_t>(index)] : nullptr;
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
            TaskRow const* row = RowAt(static_cast<int>(change.lParam));
            if (!filling && checkChanged && row && toggled)
            {
                // Not inside the list's own notification: the change refills the list.
                UiQueue::Post("tasks: complete", [this, copy = *row] { toggled(copy); });
            }
            return 0;
        }
        case NM_DBLCLK:
            if (TaskRow const* row = RowAt(reinterpret_cast<NMITEMACTIVATE const&>(note).iItem); row && opened)
            {
                opened(*row);
            }
            return 0;
        case NM_RCLICK:
        {
            auto const& activate = reinterpret_cast<NMITEMACTIVATE const&>(note);
            if (TaskRow const* row = RowAt(activate.iItem); row && menuRequested)
            {
                POINT screen = activate.ptAction;
                ClientToScreen(list, &screen);
                menuRequested(*row, screen);
            }
            return TRUE;
        }
        case NM_CUSTOMDRAW:
        {
            auto& draw = const_cast<NMLVCUSTOMDRAW&>(reinterpret_cast<NMLVCUSTOMDRAW const&>(note));
            if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
            if (draw.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
            {
                if (TaskRow const* row = RowAt(static_cast<int>(draw.nmcd.lItemlParam)))
                {
                    bool dueColumn = draw.iSubItem == 1;
                    draw.clrText = row->task.done ? GetSysColor(COLOR_GRAYTEXT)
                        : dueColumn && row->overdue ? RGB(0xC4, 0x2B, 0x1C)
                        : dueColumn && row->dueSoon ? GetSysColor(COLOR_HOTLIGHT)
                        : dueColumn ? GetSysColor(COLOR_GRAYTEXT) : GetSysColor(COLOR_WINDOWTEXT);
                }
            }
            return CDRF_DODEFAULT;
        }
        }
        return 0;
    }
}
