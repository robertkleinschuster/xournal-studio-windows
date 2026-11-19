#include "PageCommands.h"

#include "Dialogs.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/model/XoppFormat.h"

#include <algorithm>
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
        InsertBlankAt(view.PageCount() == 0 ? 0 : Current() + 1);
    }

    void PageCommands::DeleteCurrent()
    {
        DeleteAt(Current());
    }

    void PageCommands::InsertBlankAt(int index)
    {
        index = std::clamp(index, 0, view.PageCount());
        auto like = view.PageAt(index > 0 ? index - 1 : 0);
        double width = like ? like->Width() : XoppFormat::A4Width;
        double height = like ? like->Height() : XoppFormat::A4Height;
        view.InsertPage(index, XoppPage::CreateGraph(width, height));
    }

    void PageCommands::DeleteAt(int page)
    {
        if (view.PageCount() <= 1 || page < 0 || page >= view.PageCount())
        {
            return; // a document keeps one page (menu and button are disabled then)
        }
        if (Dialogs::Confirm(owner, std::format(L"Seite {} löschen?", page + 1),
                L"Das Löschen einer Seite kann nicht rückgängig gemacht werden.", L"Löschen"))
        {
            view.RemovePage(page);
        }
    }
}
