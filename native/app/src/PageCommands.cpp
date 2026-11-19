#include "PageCommands.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/model/XoppFormat.h"

#include <commctrl.h>

#include <format>

namespace xournal::app
{
    PageCommands::PageCommands(HWND owner, inkpages::WritingSurface& surface, DocumentView& view)
        : owner(owner), surface(surface), view(view)
    {
    }

    int PageCommands::Current() const
    {
        return surface.PageInView().value_or(0);
    }

    void PageCommands::InsertAfterCurrent()
    {
        int count = view.PageCount();
        int index = count == 0 ? 0 : Current() + 1;
        auto like = view.PageAt(std::max(0, index - 1));
        double width = like ? like->Width() : XoppFormat::A4Width;
        double height = like ? like->Height() : XoppFormat::A4Height;
        view.InsertPage(index, XoppPage::CreateGraph(width, height));
    }

    void PageCommands::DeleteCurrent()
    {
        int count = view.PageCount();
        if (count <= 1)
        {
            return; // a document keeps one page (menu and button are disabled then)
        }
        int page = Current();
        std::wstring instruction = std::format(L"Seite {} löschen?", page + 1);
        TASKDIALOG_BUTTON buttons[] = { { IDYES, L"Löschen" }, { IDCANCEL, L"Abbrechen" } };
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Xournal Studio";
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = L"Das Löschen einer Seite kann nicht rückgängig gemacht werden.";
        config.cButtons = ARRAYSIZE(buttons);
        config.pButtons = buttons;
        config.nDefaultButton = IDCANCEL;
        int pressed = 0;
        Check("TaskDialogIndirect (delete page)", TaskDialogIndirect(&config, &pressed, nullptr, nullptr));
        if (pressed == IDYES)
        {
            view.RemovePage(page);
        }
    }
}
