#include "GoToPageDialog.h"

#include "Resources.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>

#include <format>
#include <string>

namespace xournal::app::GoToPageDialog
{
    namespace
    {
        struct State
        {
            int current = 0, count = 1;
            int chosen = -1;
        };

        INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
        {
            try
            {
                switch (message)
                {
                case WM_INITDIALOG:
                {
                    auto state = reinterpret_cast<State*>(lp);
                    SetWindowLongPtrW(dialog, DWLP_USER, lp);
                    std::wstring label = std::format(L"&Seite (1 – {}):", state->count);
                    SetDlgItemTextW(dialog, IDC_GOTO_LABEL, label.c_str());
                    HWND spin = GetDlgItem(dialog, IDC_GOTO_SPIN);
                    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(GetDlgItem(dialog, IDC_GOTO_PAGE)), 0);
                    SendMessageW(spin, UDM_SETRANGE32, 1, state->count);
                    SendMessageW(spin, UDM_SETPOS32, 0, state->current + 1);
                    HWND edit = GetDlgItem(dialog, IDC_GOTO_PAGE);
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                    SetFocus(edit);
                    return FALSE; // the focus is set
                }
                case WM_COMMAND:
                    if (LOWORD(wp) == IDOK)
                    {
                        auto state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
                        BOOL failed = FALSE;
                        int page = static_cast<int>(SendMessageW(GetDlgItem(dialog, IDC_GOTO_SPIN), UDM_GETPOS32, 0,
                            reinterpret_cast<LPARAM>(&failed)));
                        if (failed)
                        {
                            // Typed text outside the range: say so at the box, the dialog stays.
                            std::wstring text = std::format(L"Bitte eine Seite von 1 bis {} eingeben.", state->count);
                            EDITBALLOONTIP tip{ sizeof(tip), L"Gehe zu Seite", text.c_str(), TTI_INFO };
                            SendMessageW(GetDlgItem(dialog, IDC_GOTO_PAGE), EM_SHOWBALLOONTIP, 0, reinterpret_cast<LPARAM>(&tip));
                            return TRUE;
                        }
                        state->chosen = page - 1;
                        EndDialog(dialog, IDOK);
                        return TRUE;
                    }
                    if (LOWORD(wp) == IDCANCEL)
                    {
                        EndDialog(dialog, IDCANCEL);
                        return TRUE;
                    }
                    break;
                }
            }
            catch (...)
            {
                LogException("Gehe zu Seite");
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        }
    }

    std::optional<int> Ask(HWND owner, int current, int count)
    {
        if (count <= 0) return std::nullopt;
        State state{ current, count };
        INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_GOTO_PAGE), owner, Proc,
            reinterpret_cast<LPARAM>(&state));
        if (result == -1) ThrowLastError("DialogBoxParam(Gehe zu Seite)");
        if (result != IDOK || state.chosen < 0) return std::nullopt;
        Log(std::format("go to page {} of {}", state.chosen + 1, count));
        return state.chosen;
    }
}
