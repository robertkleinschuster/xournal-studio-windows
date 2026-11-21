#include "TaskEditDialog.h"

#include "Dialogs.h"
#include "Resources.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>
#include <windowsx.h>

#include <format>

namespace xournal::app::TaskEditDialog
{
    namespace
    {
        struct State
        {
            Values values;
            std::wstring linkText;
            bool canLink = false;
        };

        INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
        {
            try
            {
                if (auto result = Dialogs::ControlColor(dialog, message, wp, lp)) return result;
                switch (message)
                {
                case WM_INITDIALOG:
                {
                    auto* state = reinterpret_cast<State*>(lp);
                    Dialogs::SetState(dialog, lp);
                    if (!SetDlgItemTextW(dialog, IDC_TASK_TEXT, state->values.text.c_str()))
                        ThrowLastError("SetDlgItemText (task edit)");
                    HWND due = GetDlgItem(dialog, IDC_TASK_DUE);
                    if (state->values.due)
                    {
                        SYSTEMTIME day{ static_cast<WORD>(state->values.due->year), static_cast<WORD>(state->values.due->month), 0,
                            static_cast<WORD>(state->values.due->day) };
                        if (!DateTime_SetSystemtime(due, GDT_VALID, &day))
                            ThrowHr("DTM_SETSYSTEMTIME (task edit)", E_FAIL);
                    }
                    else if (!DateTime_SetSystemtime(due, GDT_NONE, nullptr))
                        ThrowHr("DTM_SETSYSTEMTIME (task edit)", E_FAIL);
                    HWND link = GetDlgItem(dialog, IDC_TASK_LINK);
                    if (!state->linkText.empty())
                    {
                        SetWindowTextW(link, std::format(L"Verknüpft mit {}", state->linkText).c_str());
                        Button_SetCheck(link, BST_CHECKED);
                    }
                    else
                    {
                        EnableWindow(link, state->canLink);
                    }
                    return TRUE; // the text box gets the focus
                }
                case WM_COMMAND:
                {
                    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
                    if (LOWORD(wp) == IDOK)
                    {
                        HWND text = GetDlgItem(dialog, IDC_TASK_TEXT);
                        int length = GetWindowTextLengthW(text);
                        std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                        GetWindowTextW(text, value.data(), length + 1);
                        value.resize(static_cast<size_t>(length));
                        if (Trim(value).empty())
                        {
                            MessageBeep(MB_ICONWARNING);
                            SetFocus(text);
                            return TRUE;
                        }
                        SYSTEMTIME chosen{};
                        std::optional<Date> date;
                        auto dateState = DateTime_GetSystemtime(GetDlgItem(dialog, IDC_TASK_DUE), &chosen);
                        if (dateState == GDT_ERROR) ThrowHr("DTM_GETSYSTEMTIME (task edit)", E_FAIL);
                        if (dateState == GDT_VALID)
                            date = Date{ chosen.wYear, chosen.wMonth, chosen.wDay };
                        state->values.text = std::wstring(Trim(value));
                        state->values.due = date;
                        state->values.link = Button_GetCheck(GetDlgItem(dialog, IDC_TASK_LINK)) == BST_CHECKED;
                        if (!EndDialog(dialog, IDOK)) ThrowLastError("EndDialog (task edit)");
                        return TRUE;
                    }
                    if (LOWORD(wp) == IDCANCEL)
                    {
                        if (!EndDialog(dialog, IDCANCEL)) ThrowLastError("EndDialog (task edit)");
                        return TRUE;
                    }
                    break;
                }
                }
            }
            catch (...)
            {
                LogException(std::format("task edit dialog: message 0x{:X}", message));
                if (message == WM_INITDIALOG)
                {
                    if (!EndDialog(dialog, IDCANCEL)) LogError("EndDialog (task edit initialization) failed");
                    return TRUE;
                }
            }
            return FALSE;
        }
    }

    std::optional<Values> Show(HWND owner, Values const& values, std::wstring const& linkText, bool canLink)
    {
        State state{ values, linkText, canLink };
        INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TASK_EDIT), owner, Proc,
            reinterpret_cast<LPARAM>(&state));
        if (result <= 0)
        {
            ThrowLastError("DialogBoxParam(task)");
        }
        return result == IDOK ? std::optional(state.values) : std::nullopt;
    }
}
