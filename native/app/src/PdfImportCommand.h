#pragma once

// Datei → PDF mit iPad-Anmerkungen importieren (the C# PdfImportCommand): the user picks an annotated PDF and the .xopp
// to write; its PDF (the source without Apple's markup) goes beside it with the same name; the markup becomes the
// document's strokes, texts and pictures (native/import). The source stays as it is; a target that would replace the
// source or the open document (or its PDF) is refused; an existing PDF of that name is replaced only when the user says
// so, and only once everything else worked (it is staged as "<pdf>~import" first). Temporary files are deleted.
// The work runs on the thread pool (RenderPictures needs Windows.Data.Pdf off the STA); afterwards the new document is
// opened (after the usual question when the open one has changes).

#include "DocumentSession.h"
#include "Toast.h"

#include <windows.h>

#include <functional>
#include <string>

namespace xournal::app
{
    class PdfImportCommand
    {
    public:
        PdfImportCommand(HWND owner, DocumentSession& session, std::function<void(std::wstring const&, ToastKind)> message);

        void Import();

    private:
        HWND owner;
        DocumentSession& session;
        std::function<void(std::wstring const&, ToastKind)> message;
        bool running = false;
    };
}
