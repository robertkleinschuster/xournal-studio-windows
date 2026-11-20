#include "InsertPagesDialog.h"

#include "Dialogs.h"
#include "Resources.h"
#include "ThumbnailDrawing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr int ThumbWidthDip = 76;
        constexpr int PreviewColumn = 1;

        struct DialogState
        {
            InsertPagesRequest const& request;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true); // thumbnails arriving after the dialog closed
            std::vector<std::shared_ptr<CpuBitmap>> thumbnails;
            std::optional<InsertPagesChoice> choice;
            HWND dialog = nullptr;
            HWND list = nullptr;
            HIMAGELIST images = nullptr;
            SIZE box{}; // maximum preview size in pixels
        };

        int Scaled(int dip, HWND window) { return MulDiv(dip, static_cast<int>(GetDpiForWindow(window)), 96); }

        void UpdateOk(DialogState& state)
        {
            bool any = false;
            for (int i = 0; i < state.request.count && !any; ++i) any = ListView_GetCheckState(state.list, i) != FALSE;
            EnableWindow(GetDlgItem(state.dialog, IDOK), any);
        }

        void Init(DialogState& state)
        {
            HWND dialog = state.dialog;
            auto const& r = state.request;
            SetWindowTextW(dialog, std::format(L"Seiten aus „{}“ einfügen", r.file).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_COUNT,
                r.count == 1 ? L"1 Seite" : std::format(L"{} Seiten – gewünschte Seiten antippen.", r.count).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_AFTER, std::format(L"nach Seite {}", r.around + 1).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_BEFORE, std::format(L"vor Seite {}", r.around + 1).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_WHERE, r.where.c_str());
            CheckRadioButton(dialog, IDC_INSERT_AFTER, IDC_INSERT_BEFORE, IDC_INSERT_AFTER);

            state.list = GetDlgItem(dialog, IDC_INSERT_LIST);
            HRESULT hr = SetWindowTheme(state.list, L"Explorer", nullptr);
            if (FAILED(hr)) LogError(std::format("insert dialog: theme not set: {}", DescribeHr(hr)));
            ListView_SetExtendedListViewStyle(state.list, LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
            int width = Scaled(ThumbWidthDip, dialog);
            state.box = { width, static_cast<LONG>(std::lround(width * 1.4142)) };
            // The small image list sets the native report row height. The checkbox stays in the first column.
            state.images = CreateSlotImages({ 1, state.box.cy + Scaled(8, dialog) });
            ListView_SetImageList(state.list, state.images, LVSIL_SMALL);
            wchar_t const* titles[] = { L"Seite", L"Vorschau" };
            for (int column = 0; column < ARRAYSIZE(titles); ++column)
            {
                LVCOLUMNW info{ LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM };
                info.pszText = const_cast<wchar_t*>(titles[column]);
                info.cx = Scaled(120, dialog);
                info.iSubItem = column;
                if (ListView_InsertColumn(state.list, column, &info) == -1)
                    ThrowHr("LVM_INSERTCOLUMN (insert pages)", E_FAIL);
            }

            state.thumbnails.resize(r.count);
            for (int i = 0; i < r.count; ++i)
            {
                std::wstring text = std::to_wstring(i + 1);
                LVITEMW item{ LVIF_TEXT | LVIF_IMAGE };
                item.iItem = i;
                item.pszText = text.data();
                item.iImage = 0;
                if (ListView_InsertItem(state.list, &item) == -1)
                    ThrowHr("LVM_INSERTITEM (insert pages)", E_FAIL);
            }
            if (!ListView_SetColumnWidth(state.list, PreviewColumn, LVSCW_AUTOSIZE_USEHEADER))
                ThrowHr("LVM_SETCOLUMNWIDTH (insert pages)", E_FAIL);
            LVCOLUMNW fixed{ LVCF_FMT, LVCFMT_LEFT | LVCFMT_FIXED_WIDTH };
            for (int column = 0; column < ARRAYSIZE(titles); ++column)
                if (!ListView_SetColumn(state.list, column, &fixed))
                    ThrowHr("LVM_SETCOLUMN (insert pages)", E_FAIL);
            if (r.count == 1) ListView_SetCheckState(state.list, 0, TRUE); // one page: nothing to choose
            UpdateOk(state);

            for (int i = 0; i < r.count; ++i)
            {
                std::weak_ptr<bool> alive = state.alive;
                DialogState* target = &state;
                SIZE pixels = ThumbnailSize(state.box, r.aspect ? r.aspect(i) : 1.4142);
                r.render(i, pixels, [alive, target, i](std::shared_ptr<CpuBitmap> bitmap)
                {
                    if (alive.expired() || !bitmap) return; // the dialog closed meanwhile, or the page cannot be shown
                    target->thumbnails[i] = std::move(bitmap);
                    ListView_RedrawItems(target->list, i, i);
                });
            }
        }

        LRESULT OnNotify(DialogState& state, NMHDR const& header)
        {
            if (header.idFrom == IDC_INSERT_ALL && (header.code == NM_CLICK || header.code == NM_RETURN))
            {
                for (int i = 0; i < state.request.count; ++i) ListView_SetCheckState(state.list, i, TRUE);
                UpdateOk(state);
                return TRUE;
            }
            if (header.hwndFrom != state.list) return FALSE;
            switch (header.code)
            {
            case NM_CLICK:
            {
                // A tap on a page checks or unchecks it (not only a tap on its small box).
                auto const& click = reinterpret_cast<NMITEMACTIVATE const&>(header);
                LVHITTESTINFO hit{ click.ptAction };
                int item = ListView_SubItemHitTest(state.list, &hit);
                if (item >= 0 && !(hit.flags & LVHT_ONITEMSTATEICON))
                {
                    ListView_SetCheckState(state.list, item, !ListView_GetCheckState(state.list, item));
                }
                return FALSE;
            }
            case LVN_ITEMCHANGED:
                UpdateOk(state);
                return FALSE;
            case NM_CUSTOMDRAW:
            {
                auto& draw = const_cast<NMLVCUSTOMDRAW&>(reinterpret_cast<NMLVCUSTOMDRAW const&>(header));
                LRESULT result = CDRF_DODEFAULT;
                if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) result = CDRF_NOTIFYITEMDRAW;
                else if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) result = CDRF_NOTIFYPOSTPAINT;
                else if (draw.nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT)
                {
                    int index = static_cast<int>(draw.nmcd.dwItemSpec);
                    RECT preview{};
                    if (index < static_cast<int>(state.thumbnails.size()) &&
                        ListView_GetSubItemRect(state.list, index, PreviewColumn, LVIR_BOUNDS, &preview))
                    {
                        double aspect = state.request.aspect ? state.request.aspect(index) : 1.4142;
                        preview.right = std::min(preview.right, preview.left + state.box.cx + Scaled(12, state.dialog));
                        DrawPageThumbnail(draw.nmcd.hdc, preview, state.box, aspect, state.thumbnails[index].get());
                    }
                }
                SetWindowLongPtrW(state.dialog, DWLP_MSGRESULT, result);
                return TRUE;
            }
            }
            return FALSE;
        }

        INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
        {
            try
            {
                if (message == WM_INITDIALOG) Dialogs::SetState(dialog, lp);
                auto state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
                if (!state) return FALSE;
                switch (message)
                {
                case WM_INITDIALOG:
                    state->dialog = dialog;
                    Init(*state);
                    return TRUE;
                case WM_NOTIFY:
                    return OnNotify(*state, *reinterpret_cast<NMHDR const*>(lp));
                case WM_COMMAND:
                    if (LOWORD(wp) == IDOK)
                    {
                        InsertPagesChoice choice;
                        for (int i = 0; i < state->request.count; ++i)
                            if (ListView_GetCheckState(state->list, i)) choice.pages.push_back(i);
                        choice.before = IsDlgButtonChecked(dialog, IDC_INSERT_BEFORE) == BST_CHECKED;
                        state->choice = std::move(choice);
                        if (!EndDialog(dialog, IDOK)) ThrowLastError("EndDialog (insert pages)");
                        return TRUE;
                    }
                    if (LOWORD(wp) == IDCANCEL)
                    {
                        if (!EndDialog(dialog, IDCANCEL)) ThrowLastError("EndDialog (insert pages)");
                        return TRUE;
                    }
                    return FALSE;
                }
            }
            catch (...)
            {
                LogException(std::format("insert dialog: message 0x{:X}", message));
                if (message == WM_INITDIALOG)
                {
                    if (!EndDialog(dialog, IDCANCEL)) LogError("EndDialog (insert pages initialization) failed");
                    return TRUE;
                }
            }
            return FALSE;
        }
    }

    std::optional<InsertPagesChoice> AskInsertPages(HWND owner, InsertPagesRequest const& request)
    {
        DialogState state{ request };
        INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INSERT_PAGES), owner, Proc,
            reinterpret_cast<LPARAM>(&state));
        DWORD error = result <= 0 ? GetLastError() : ERROR_SUCCESS;
        state.alive.reset(); // thumbnails still on their way are dropped
        if (state.images && !ImageList_Destroy(state.images)) LogError("ImageList_Destroy (insert pages) failed");
        if (result <= 0) ThrowHr("DialogBoxParam (insert pages)", HRESULT_FROM_WIN32(error));
        return result == IDOK ? state.choice : std::nullopt;
    }
}
