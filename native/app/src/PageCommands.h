#pragma once

// Seite einfügen / Seite löschen, as the C# app: a graph page of the size of the page in view is inserted after it; the
// page in view is deleted after a question (it cannot be undone; the last page stays). Strokes on other pages keep
// their undo history (InkPages' page operations).

#include "DocumentView.h"

#include "inkpages/WritingSurface.h"

#include <windows.h>

namespace xournal::app
{
    class PageCommands
    {
    public:
        PageCommands(HWND owner, inkpages::WritingSurface& surface, DocumentView& view);

        void InsertAfterCurrent();
        void DeleteCurrent();
        /// A graph page at `index`, the size of the page before it (else after it).
        void InsertBlankAt(int index);
        /// Deletes page `page` after the question (the last page stays).
        void DeleteAt(int page);

    private:
        int Current() const;

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
    };
}
