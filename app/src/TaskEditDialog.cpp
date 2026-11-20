#include "TaskEditDialog.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <commctrl.h>
#include <windowsx.h>

#include <format>
#include <vector>

namespace xournal::app::TaskEditDialog
{
    namespace
    {
        enum ControlId : int { TextId = 100, DueId, LinkId, HasDueId };
        constexpr WORD ButtonClass = 0x0080, EditClass = 0x0081, StaticClass = 0x0082;

        // A DLGTEMPLATEEX in memory (the layout Windows documents for extended dialog templates).
        class Template
        {
        public:
            Template(wchar_t const* title, short cx, short cy, WORD items)
            {
                Word(1);                    // dlgVer
                Word(0xFFFF);               // signature
                Dword(0);                   // helpID
                Dword(0);                   // exStyle
                Dword(DS_MODALFRAME | DS_SHELLFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
                Word(items);
                Word(0); Word(0); Word(static_cast<WORD>(cx)); Word(static_cast<WORD>(cy));
                Word(0);                    // no menu
                Word(0);                    // the dialog class
                Text(title);
                Word(9); Word(FW_NORMAL); Word(0); // 9 pt, weight, italic + charset
                Text(L"MS Shell Dlg 2");    // the shell font (Segoe UI)
            }

            void Item(DWORD style, short x, short y, short cx, short cy, int id, WORD atom, wchar_t const* className,
                wchar_t const* text)
            {
                while (data.size() % 2) Word(0); // DWORD aligned
                Dword(0);                   // helpID
                Dword(0);                   // exStyle
                Dword(WS_CHILD | WS_VISIBLE | style);
                Word(static_cast<WORD>(x)); Word(static_cast<WORD>(y)); Word(static_cast<WORD>(cx)); Word(static_cast<WORD>(cy));
                Dword(static_cast<DWORD>(id));
                if (className)
                {
                    Text(className);
                }
                else
                {
                    Word(0xFFFF);
                    Word(atom);
                }
                Text(text);
                Word(0);                    // no creation data
            }

            DLGTEMPLATE const* Get() const { return reinterpret_cast<DLGTEMPLATE const*>(data.data()); }

        private:
            void Word(WORD w) { data.push_back(w); }
            void Dword(DWORD d) { Word(LOWORD(d)); Word(HIWORD(d)); }
            void Text(wchar_t const* s)
            {
                for (; *s; ++s) Word(static_cast<WORD>(*s));
                Word(0);
            }

            std::vector<WORD> data; // WORD elements: the template starts DWORD aligned (vector storage)
        };

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
                switch (message)
                {
                case WM_INITDIALOG:
                {
                    auto* state = reinterpret_cast<State*>(lp);
                    SetWindowLongPtrW(dialog, DWLP_USER, lp);
                    SetDlgItemTextW(dialog, TextId, state->values.text.c_str());
                    // "Fällig am:" checked = a due date (the picker shows it); unchecked = none (the picker, disabled,
                    // shows today for when the box gets checked).
                    HWND due = GetDlgItem(dialog, DueId);
                    if (state->values.due)
                    {
                        SYSTEMTIME day{ static_cast<WORD>(state->values.due->year), static_cast<WORD>(state->values.due->month), 0,
                            static_cast<WORD>(state->values.due->day) };
                        if (!DateTime_SetSystemtime(due, GDT_VALID, &day)) LogError("task edit dialog: due date not shown");
                    }
                    CheckDlgButton(dialog, HasDueId, state->values.due ? BST_CHECKED : BST_UNCHECKED);
                    EnableWindow(due, state->values.due.has_value());
                    HWND link = GetDlgItem(dialog, LinkId);
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
                        HWND text = GetDlgItem(dialog, TextId);
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
                        state->values.text = std::wstring(Trim(value));
                        SYSTEMTIME chosen{};
                        state->values.due.reset();
                        if (IsDlgButtonChecked(dialog, HasDueId) == BST_CHECKED &&
                            DateTime_GetSystemtime(GetDlgItem(dialog, DueId), &chosen) == GDT_VALID)
                        {
                            state->values.due = Date{ chosen.wYear, chosen.wMonth, chosen.wDay };
                        }
                        state->values.link = Button_GetCheck(GetDlgItem(dialog, LinkId)) == BST_CHECKED;
                        EndDialog(dialog, IDOK);
                        return TRUE;
                    }
                    if (LOWORD(wp) == HasDueId && HIWORD(wp) == BN_CLICKED)
                    {
                        EnableWindow(GetDlgItem(dialog, DueId), IsDlgButtonChecked(dialog, HasDueId) == BST_CHECKED);
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
            }
            catch (...)
            {
                LogException(std::format("task edit dialog: message 0x{:X}", message));
            }
            return FALSE;
        }
    }

    std::optional<Values> Show(HWND owner, Values const& values, std::wstring const& linkText, bool canLink)
    {
        Template dialog(L"Aufgabe bearbeiten", 240, 104, 7);
        dialog.Item(SS_LEFT, 7, 7, 226, 9, -1, StaticClass, nullptr, L"&Aufgabe");
        dialog.Item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 7, 18, 226, 14, TextId, EditClass, nullptr, L"");
        // Windows' dialog layout: a check box "Fällig am:" with the date picker beside it (enabled while checked).
        dialog.Item(WS_TABSTOP | BS_AUTOCHECKBOX, 7, 41, 56, 10, HasDueId, ButtonClass, nullptr, L"&Fällig am:");
        dialog.Item(WS_TABSTOP | DTS_SHORTDATEFORMAT, 65, 39, 90, 14, DueId, 0, DATETIMEPICK_CLASSW, L"");
        dialog.Item(WS_TABSTOP | BS_AUTOCHECKBOX, 7, 60, 226, 10, LinkId, ButtonClass, nullptr, L"Mit dieser Seite &verknüpfen");
        dialog.Item(WS_TABSTOP | BS_DEFPUSHBUTTON, 129, 83, 50, 14, IDOK, ButtonClass, nullptr, L"Speichern");
        dialog.Item(WS_TABSTOP | BS_PUSHBUTTON, 183, 83, 50, 14, IDCANCEL, ButtonClass, nullptr, L"Abbrechen");
        State state{ values, linkText, canLink };
        INT_PTR result = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), dialog.Get(), owner, Proc,
            reinterpret_cast<LPARAM>(&state));
        if (result == -1)
        {
            ThrowLastError("DialogBoxIndirectParam(task)");
        }
        return result == IDOK ? std::optional(state.values) : std::nullopt;
    }
}
