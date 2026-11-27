#pragma once

// Datei → Als PDF exportieren and Datei → Für iPad exportieren (the same flow; the second writes the pen strokes as the
// iPad's editable ink annotations, import module): asks where (the document's name in its folder; never over its background PDF, so a name
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

        enum class Kind { Pdf, Ipad };
        void Export(std::shared_ptr<XoppDocument> const& doc, Kind kind = Kind::Pdf);

    private:
        HWND owner;
        DocumentView& view;
        std::function<void(std::wstring const&, MessageKind)> message;
        bool running = false;
    };
}
