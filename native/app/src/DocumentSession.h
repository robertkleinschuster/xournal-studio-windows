#pragma once

// The open document and what happens to it: new, open (.xopp, or a PDF as a new document on it), save, save as, and
// the one question whenever a document with changes is left (Speichern / Nicht speichern / Abbrechen). Saving only by
// choice: nothing is saved behind the user's back. Loading runs on the thread pool; saving on the UI thread.

#include "DocumentView.h"
#include "Toast.h"

#include "xournal/model/XoppDocument.h"

#include "inkpages/WritingSurface.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace xournal::app
{
    class DocumentSession
    {
    public:
        DocumentSession(HWND owner, inkpages::WritingSurface& surface, DocumentView& view);

        /// Shown after any change of the document or its saved state (title, last file).
        std::function<void()> changed;
        /// Messages for the toast.
        std::function<void(std::wstring const&, ToastKind)> message;

        void New();
        void Open();                                 // asks for the file
        void Open(std::filesystem::path const& file); // e.g. from the explorer or the last session
        bool Save();                                 // false: not saved (cancelled or failed)
        bool SaveAs();

        /// Asks about unsaved changes; true when the document may be left (saved, discarded or unchanged).
        bool ConfirmLeave();

        bool IsModified() const;
        std::wstring Title() const;                  // "Name – Xournal Studio" with a • when modified
        std::optional<std::filesystem::path> FilePath() const;

    private:
        void Show(std::shared_ptr<XoppDocument> doc);
        bool SaveTo(std::filesystem::path const& file, bool saveAs);
        void Fail(std::wstring const& what);

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
        std::shared_ptr<XoppDocument> doc;
        bool loading = false;
    };
}
