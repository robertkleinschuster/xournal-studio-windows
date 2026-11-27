#pragma once

// The open documents and what happens to them: new, open (.xopp, or a PDF as a new document on it), save, save as, and
// the one question whenever a document with changes is left (Speichern / Nicht speichern / Abbrechen). Saving only by
// choice: nothing is saved behind the user's back.
//
// Tabs (as in an IDE): every open document is a tab; New and Open add one (a file already open is only activated; an
// untouched new document is replaced). One tab is shown on the writing surface, the others are parked: the ink is
// brought into the model, the unsaved state and the position are kept with the tab, and the surface's history starts
// over when the tab is shown again (undo reaches back only to the last tab switch). Closing a tab with changes asks. Loading runs on the thread pool; saving on the UI thread. While a
// document loads the shown one can still be edited: when the loaded one arrives after such an edit the leave question is
// asked again (Abbrechen keeps the shown document), and a document shown meanwhile (a new one, a recovered copy) wins.

#include "DocumentView.h"
#include "MessageKind.h"

#include "xournal/model/XoppDocument.h"

#include "inkpages/WritingSurface.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace xournal::app
{
    class DocumentSession
    {
    public:
        DocumentSession(HWND owner, inkpages::WritingSurface& surface, DocumentView& view);

        /// Shown after any change of the document or its saved state (title, last file).
        std::function<void()> changed;
        /// Messages for the status bar.
        std::function<void(std::wstring const&, MessageKind)> message;

        /// A new, empty document in a new tab.
        void New();
        void Open();                                 // asks for the file
        /// A new tab for the file (or the tab that has it); an untouched new document is replaced. `position` >= 0:
        /// the tab takes that place among the tabs (a document opened again where it was).
        void Open(std::filesystem::path const& file, int position = -1); // e.g. from the explorer or the last session
        bool Save();                                 // false: not saved (cancelled or failed)
        bool SaveAs();

        /// Shows a restored recovery copy in a new tab: it counts as changed (unsaved) until it is saved. False: not
        /// shown (the shown tab could not be parked).
        bool ShowRecovered(std::shared_ptr<XoppDocument> restored);
        /// `item` (a file or folder the document uses: its own file, its PDF, a picture background, a folder holding them)
        /// was renamed or moved to `renamed`: the document refers to them there - and is the file there - and counts as
        /// changed, so the next save writes the references from the new place (and leaving it asks).
        void MoveFiles(std::filesystem::path const& item, std::filesystem::path const& renamed);
        /// "Meine als Kopie speichern…": a copy with its own attachments; the document stays as it is.
        bool SaveCopy();
        /// While a sync runs: says so for "<what>" ("Öffnen", "Speichern") and returns true; nothing is opened or
        /// saved then (git works on the files).
        std::function<bool(std::wstring const& what)> refusedWhileSyncing;
        /// While a sync runs, Speichern (Strg+S) waits for it: returns true when the save was put off (said).
        std::function<bool()> saveDeferredWhileSyncing;
        /// Where the save dialog opens for a document never saved: the folder selected in the explorer, else the project
        /// folder (user, 2026-11-20); none: the PDF's folder.
        std::function<std::optional<std::filesystem::path>()> folderForNew;
        /// Another document is about to be shown: the shown one is left (its position can be remembered by PlaceKey).
        std::function<void()> leaving;
        /// Where to show a document remembered by `key` (none: the default - the page width at the top).
        std::function<std::optional<inkpages::ViewPlace>(std::filesystem::path const& key)> placeOf;
        /// A file was opened or saved under its name (Zuletzt geöffnet).
        std::function<void(std::filesystem::path const&)> fileUsed;
        // ---- tabs
        /// The tab strip is to be rebuilt (a tab was added, closed, renamed, or its unsaved state changed).
        std::function<void()> tabsChanged;
        int TabCount() const { return static_cast<int>(tabs.size()); }
        int ActiveTab() const { return static_cast<int>(active); }
        std::wstring TabName(int index) const;      // the document's name
        bool TabModified(int index) const;
        std::optional<std::filesystem::path> TabFile(int index) const; // its file (else the PDF it was opened from)
        /// Shows tab `index`; false when the shown one could not be parked (said).
        bool Activate(int index);
        /// Closes a tab: shown first, then the leave question; false when the user cancelled. The last tab closed leaves
        /// a new document.
        bool CloseTab(int index);
        /// Removes a tab without a question (the caller asked already, or its content is replaced by a reload).
        void DropTab(int index);
        bool AnyModified(); // ends the Text tool's editing first (the typed text is a change)
        /// Saves every tab with changes (each is shown while it is saved; a document never saved asks for its place);
        /// false when one was not saved.
        bool SaveAll();
        /// The window closes: every tab with changes is shown and asked about; false when the user cancelled.
        bool ConfirmLeaveAll();
        /// The documents of all tabs (the shown one included).
        std::vector<std::shared_ptr<XoppDocument>> Documents() const;
        /// The tab showing `document`, or -1.
        int TabOf(std::shared_ptr<XoppDocument> const& document) const;
        /// The tabs whose document uses `item` (its file, PDF or a picture background, or a folder holding them).
        std::vector<int> TabsUsing(std::filesystem::path const& item) const;
        /// The file the shown document's position is remembered by: its own file, else the PDF it was opened from.
        std::optional<std::filesystem::path> PlaceKey() const;

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
        struct Tab
        {
            std::shared_ptr<XoppDocument> doc;
            std::optional<std::filesystem::path> openedFrom;
            bool modified = false; // parked tabs: the state when it was parked
            std::optional<inkpages::ViewPlace> place;
        };

        /// Shows `doc` in the shown tab (`newTab` false) or in a new one. False: the shown tab could not be parked.
        bool Show(std::shared_ptr<XoppDocument> doc, std::optional<std::filesystem::path> openedFrom, bool newTab);
        /// Loads in the background and shows the document in a new tab at `position` (-1: at the end); `replacing`: a
        /// new document nothing was done with, which goes when the loaded one is shown.
        void Load(std::filesystem::path const& file, int position, std::shared_ptr<XoppDocument> replacing);
        void InsertParked(std::shared_ptr<XoppDocument> document, std::optional<std::filesystem::path> from, int position);
        bool IsPristineTab(size_t index) const;
        void MoveLastTabTo(int position);
        /// The shown tab's content is brought into the model and kept with the tab (only when it has changes).
        bool Park();
        /// Shows the tab `index` (the shown one is parked already).
        void Switch(size_t index);
        int TabWithFile(std::filesystem::path const& file) const;
        void OpenPending();
        bool IsPristine() const; // a new document nothing was done with
        bool SaveTo(std::filesystem::path const& file, bool saveAs);
        void Fail(std::wstring const& what);

        HWND owner;
        inkpages::WritingSurface& surface;
        DocumentView& view;
        std::shared_ptr<XoppDocument> doc;
        std::optional<std::filesystem::path> openedFrom; // the file the shown document was loaded from (.xopp or PDF)
        bool loading = false;
        std::uint64_t edits = 0;     // changes of the shown document (ink history, pages, references)
        std::uint64_t shownCount = 0; // documents shown so far: a load whose document was replaced meanwhile is dropped
        bool changedOutsideInk = false; // a restored recovery copy or moved references, not saved yet
        bool pagesChanged = false; // pages inserted or removed since the last save
        // While a question is open (leave, save as) a document that finishes loading goes into the strip without being
        // shown: the question is about the tab that is shown.
        struct QuestionScope
        {
            explicit QuestionScope(DocumentSession& s) : session(s) { ++session.asking; }
            ~QuestionScope() { --session.asking; }
            DocumentSession& session;
        };
        int asking = 0;
        std::deque<std::pair<std::filesystem::path, int>> pendingOpens; // asked for while another loaded (file, position)
        std::vector<Tab> tabs;     // the shown tab's entry is outdated (doc, openedFrom above are the live ones)
        size_t active = 0;
    };
}
