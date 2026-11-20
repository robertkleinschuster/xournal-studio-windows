#include "InsertPagesDialog.h"

#include "Resources.h"
#include "ThumbnailDrawing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <cmath>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr int ThumbWidthDip = 80; // a page about as wide as a stamp, its layout recognizable

        struct DialogState
        {
            InsertPagesRequest const& request;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true); // thumbnails arriving after the dialog closed
            std::vector<std::shared_ptr<CpuBitmap>> thumbnails;
            std::optional<InsertPagesChoice> choice;
            HWND dialog = nullptr;
            HWND list = nullptr;
            HIMAGELIST images = nullptr;
            SIZE box{}; // a thumbnail's box in the item's image slot (pixels)
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
                std::format(L"{} {} – die gewünschten antippen.", r.count, r.count == 1 ? L"Seite" : L"Seiten").c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_AFTER, std::format(L"nach Seite {}", r.around + 1).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_BEFORE, std::format(L"vor Seite {}", r.around + 1).c_str());
            SetDlgItemTextW(dialog, IDC_INSERT_WHERE, r.where.c_str());
            CheckRadioButton(dialog, IDC_INSERT_AFTER, IDC_INSERT_BEFORE, IDC_INSERT_AFTER);

            state.list = GetDlgItem(dialog, IDC_INSERT_LIST);
            HRESULT hr = SetWindowTheme(state.list, L"Explorer", nullptr);
            if (FAILED(hr)) LogError(std::format("insert dialog: theme not set: {}", DescribeHr(hr)));
            ListView_SetExtendedListViewStyle(state.list, LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
            int width = Scaled(ThumbWidthDip, dialog), height = static_cast<int>(std::lround(width * 1.4142));
            state.images = CreateSlotImages({ width, height });
            int margin = Scaled(3, dialog);
            state.box = { width - 2 * margin, height - 2 * margin };
            ListView_SetImageList(state.list, state.images, LVSIL_NORMAL);

            state.thumbnails.resize(r.count);
            for (int i = 0; i < r.count; ++i)
            {
                std::wstring text = std::to_wstring(i + 1);
                LVITEMW item{ LVIF_TEXT | LVIF_IMAGE };
                item.iItem = i;
                item.pszText = text.data();
                item.iImage = 0;
                ListView_InsertItem(state.list, &item);
            }
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
                int item = ListView_HitTest(state.list, &hit);
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
                    RECT icon{ LVIR_ICON };
                    if (index < static_cast<int>(state.thumbnails.size()) && ListView_GetItemRect(state.list, index, &icon, LVIR_ICON))
                    {
                        double aspect = state.request.aspect ? state.request.aspect(index) : 1.4142;
                        DrawPageThumbnail(draw.nmcd.hdc, icon, state.box, aspect, state.thumbnails[index].get(), nullptr);
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
            if (message == WM_INITDIALOG)
            {
                SetWindowLongPtrW(dialog, DWLP_USER, lp);
            }
            auto state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (!state) return FALSE;
            try
            {
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
                        EndDialog(dialog, IDOK);
                        return TRUE;
                    }
                    if (LOWORD(wp) == IDCANCEL)
                    {
                        EndDialog(dialog, IDCANCEL);
                        return TRUE;
                    }
                    return FALSE;
                }
            }
            catch (...)
            {
                LogException(std::format("insert dialog: message 0x{:X}", message));
            }
            return FALSE;
        }
    }

    std::optional<InsertPagesChoice> AskInsertPages(HWND owner, InsertPagesRequest const& request)
    {
        DialogState state{ request };
        INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INSERT_PAGES), owner, Proc,
            reinterpret_cast<LPARAM>(&state));
        state.alive.reset(); // thumbnails still on their way are dropped
        if (state.images) ImageList_Destroy(state.images);
        if (result == -1) ThrowLastError("DialogBoxParam (insert pages)");
        return result == IDOK ? state.choice : std::nullopt;
    }
}
