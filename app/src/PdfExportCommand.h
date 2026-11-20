#pragma once

// Datei → Als PDF exportieren: asks where (the document's name in its folder; never over its background PDF, so a name
// that would be that PDF gets " (mit Notizen)"), takes the document as it is now (saved or not) on the UI thread and
// writes the PDF with the pdf module on the thread pool. The result goes to the status bar.

#include "DocumentView.h"
#include "MessageKind.h"

#include "xournal/model/XoppDocument.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace xournal::app
{
    class PdfExportCommand
    {
    public:
        PdfExportCommand(HWND owner, DocumentView& view, std::function<void(std::wstring const&, MessageKind)> message);

        void Export(std::shared_ptr<XoppDocument> const& doc);

    private:
        HWND owner;
        DocumentView& view;
        std::function<void(std::wstring const&, MessageKind)> message;
        bool running = false;
    };
}
