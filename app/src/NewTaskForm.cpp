#include "NewTaskForm.h"

#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <windowsx.h>

namespace xournal::app
{
    NewTaskForm::NewTaskForm(HWND parent, int firstId) : addId(firstId + 3)
    {
        text = ChildControls::Create(parent, WC_EDITW, L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, firstId);
        Edit_SetCueBannerTextFocused(text, L"Neue Aufgabe (Eingabe fügt hinzu)", TRUE);
        if (!SetWindowSubclass(text, EditProc, 0, reinterpret_cast<DWORD_PTR>(this)))
        {
            ThrowLastError("SetWindowSubclass(new task)");
        }
        due = ChildControls::Create(parent, DATETIMEPICK_CLASSW, L"", WS_TABSTOP | DTS_SHORTDATEFORMAT | DTS_SHOWNONE, firstId + 1);
        DateTime_SetSystemtime(due, GDT_NONE, nullptr); // no due date until one is chosen
        linkPage = ChildControls::Create(parent, WC_BUTTONW, L"Mit dieser Seite verknüpfen", WS_TABSTOP | BS_AUTOCHECKBOX, firstId + 2);
        addButton = ChildControls::Create(parent, WC_BUTTONW, L"Hinzufügen", WS_TABSTOP | BS_PUSHBUTTON, addId);
    }

    NewTaskForm::~NewTaskForm()
    {
        if (IsWindow(text))
        {
            RemoveWindowSubclass(text, EditProc, 0);
        }
    }

    int NewTaskForm::Layout(int left, int top, int width, int dpi)
    {
        auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        int y = top;
        MoveWindow(text, left, y, width, px(26), TRUE);
        y += px(30);
        int addWidth = px(90), gap = px(8);
        MoveWindow(due, left, y, width - addWidth - gap, px(26), TRUE);
        MoveWindow(addButton, left + width - addWidth, y, addWidth, px(26), TRUE);
        y += px(30);
        MoveWindow(linkPage, left, y, width, px(22), TRUE);
        return y + px(28);
    }

    void NewTaskForm::Show(bool shown)
    {
        for (HWND control : { text, due, linkPage, addButton })
        {
            ShowWindow(control, shown ? SW_SHOW : SW_HIDE);
        }
    }

    void NewTaskForm::EnableLink(bool enabled)
    {
        EnableWindow(linkPage, enabled);
        if (!enabled) Button_SetCheck(linkPage, BST_UNCHECKED);
    }

    NewTaskForm::Entry NewTaskForm::Read() const
    {
        Entry entry;
        int length = GetWindowTextLengthW(text);
        entry.text.resize(static_cast<size_t>(length) + 1);
        GetWindowTextW(text, entry.text.data(), length + 1);
        entry.text.resize(static_cast<size_t>(length));
        SYSTEMTIME chosen{};
        if (DateTime_GetSystemtime(due, &chosen) == GDT_VALID)
        {
            entry.due = Date{ chosen.wYear, chosen.wMonth, chosen.wDay };
        }
        entry.linkPage = Button_GetCheck(linkPage) == BST_CHECKED;
        return entry;
    }

    void NewTaskForm::Clear()
    {
        SetWindowTextW(text, L"");
        DateTime_SetSystemtime(due, GDT_NONE, nullptr);
        Button_SetCheck(linkPage, BST_UNCHECKED);
    }

    bool NewTaskForm::OnCommand(int id)
    {
        if (id != addId)
        {
            return false;
        }
        if (submitted) submitted();
        return true;
    }

    LRESULT CALLBACK NewTaskForm::EditProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
    {
        // Enter in the text box adds the task (a single-line edit would only beep).
        if (message == WM_KEYDOWN && wp == VK_RETURN)
        {
            auto* form = reinterpret_cast<NewTaskForm*>(data);
            try
            {
                if (form->submitted) form->submitted();
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
}
