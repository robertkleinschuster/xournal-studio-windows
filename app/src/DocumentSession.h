#pragma once

// The open document and what happens to it: new, open (.xopp, or a PDF as a new document on it), save, save as, and
// the one question whenever a document with changes is left (Speichern / Nicht speichern / Abbrechen). Saving only by
// choice: nothing is saved behind the user's back. Loading runs on the thread pool; saving on the UI thread. While a
// document loads the shown one can still be edited: when the loaded one arrives after such an edit the leave question is
// asked again (Abbrechen keeps the shown document), and a document shown meanwhile (a new one, a recovered copy) wins.

#include "DocumentView.h"
#include "Toast.h"

#include "xournal/model/XoppDocument.h"

#include "inkpages/WritingSurface.h"

#include <cstdint>
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

        /// A new, empty document without the leave question (the caller asked already).
        void ShowNew();
        /// Opens without the leave question (the caller asked already, or the document is reloaded).
        void Load(std::filesystem::path const& file);
        /// Shows a restored recovery copy: it counts as changed (unsaved) until it is saved.
        void ShowRecovered(std::shared_ptr<XoppDocument> restored);
        /// `item` (a file or folder the document uses: its PDF, a picture background, a folder holding them) was renamed
        /// to `renamed`: the document refers to them there and counts as changed, so the next save writes the new
        /// references (and leaving it asks).
        void MoveFiles(std::filesystem::path const& item, std::filesystem::path const& renamed);
        /// "Meine als Kopie speichern…": a copy with its own attachments; the document stays as it is.
        bool SaveCopy();
        /// While a sync runs: says so for "<what>" ("Öffnen", "Speichern") and returns true; nothing is opened or
        /// saved then (git works on the files).
        std::function<bool(std::wstring const& what)> refusedWhileSyncing;
        /// While a sync runs, Speichern (Strg+S) waits for it: returns true when the save was put off (said).
        std::function<bool()> saveDeferredWhileSyncing;

        /// Ends the Text tool's editing first (the typed text is part of the document), then asks about unsaved changes;
        /// true when the document may be left (saved, discarded or unchanged).
        bool ConfirmLeave();
        /// Ends the Text tool's editing, so a text being typed is in the document before a question about changes.
        void EndTextEditing();

        /// The document changed outside the ink's history (a page inserted or removed): unsaved until saved.
        void NotePagesChanged();
        /// The ink's history changed (an edit, undo, redo): counted, so a load that ends after it asks again.
        void NoteEdit() { ++edits; }

        bool IsModified() const;
        std::wstring Title() const;                  // "Name – Xournal Studio" with a • when modified
        std::optional<std::filesystem::path> FilePath() const;
        std::shared_ptr<XoppDocument> Document() const { return doc; }

    private:
        void Show(std::shared_ptr<XoppDocument> doc);
        bool SaveTo(std::filesystem::path const& file, bool saveAs);
        void Fail(std::wstring const& what);

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
        std::shared_ptr<XoppDocument> doc;
        bool loading = false;
        std::uint64_t edits = 0;     // changes of the shown document (ink history, pages, references)
        std::uint64_t shownCount = 0; // documents shown so far: a load whose document was replaced meanwhile is dropped
        bool changedOutsideInk = false; // a restored recovery copy or moved references, not saved yet
        bool pagesChanged = false; // pages inserted or removed since the last save
    };
}
