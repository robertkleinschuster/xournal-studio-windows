#pragma once

// Seiten aus PDF oder Dokument einfügen (Seite menu and the pages pane; the C# InsertPagesCommand): pages of another PDF
// (e.g. a cover sheet) or of another document (.xopp) before or after a page, chosen as thumbnails, stored as upstream
// Xournal++ stores a document - one background PDF, pages refer to its pages by number:
//   - the document's own PDF: its pages are just referred to;
//   - a document without PDF takes the source's PDF as its PDF;
//   - otherwise the chosen PDF pages are appended to the end of the document's PDF (pdf::AppendPages, written at once
//     - the dialog says so; not during a sync).
// Pages of a .xopp are copied with everything on them (CopyPages), their pageno pointing at the document's PDF pages.
// Loading, thumbnails and appending run off the UI thread.

#include "DocumentSession.h"
#include "DocumentView.h"
#include "MessageKind.h"

#include <windows.h>

#include <functional>
#include <string>

namespace xournal::app
{
    class InsertPagesCommand
    {
    public:
        InsertPagesCommand(HWND owner, DocumentSession& session, DocumentView& view,
            std::function<void(std::wstring const&, MessageKind)> message);

        /// Inserts pages before or after page `around` (0-based; the dialog asks which).
        void Insert(int around);

    private:
        struct Source;
        void Ask(std::shared_ptr<Source> source, int around);
        void Bring(std::shared_ptr<Source> source, std::vector<int> chosen, int index);
        void Finish(std::shared_ptr<Source> source, std::vector<int> chosen, int index, std::function<int(int)> pageOf,
            bool appended, bool adopted);

        HWND owner;
        DocumentSession& session;
        DocumentView& view;
        std::function<void(std::wstring const&, MessageKind)> message;
        bool running = false;
    };
}
