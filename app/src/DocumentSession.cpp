#include "DocumentSession.h"

#include "Dialogs.h"
#include "ErrorText.h"
#include "PdfPages.h"
#include "UiQueue.h"

#include "xournal/project/ProjectFiles.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/model/Snapshot.h"

#include <algorithm>
#include <format>

namespace xournal::app
{
    namespace
    {
        bool IsPdf(std::filesystem::path const& file) { return EqualsIgnoreCase(file.extension().native(), L".pdf"); }
    }

    DocumentSession::DocumentSession(HWND owner, inkpages::WritingSurface& surface, DocumentView& view)
        : owner(owner), surface(surface), view(view)
    {
    }

    bool DocumentSession::Show(std::shared_ptr<XoppDocument> next, std::optional<std::filesystem::path> from, bool newTab)
    {
        if (doc && newTab)
        {
            if (!Park()) return false;
            tabs.emplace_back();
            active = tabs.size() - 1;
        }
        else
        {
            if (doc && leaving) leaving();
            if (tabs.empty()) tabs.emplace_back();
        }
        doc = std::move(next);
        openedFrom = std::move(from);
        ++shownCount;
        changedOutsideInk = false;
        pagesChanged = false;
        tabs[active] = { doc, openedFrom, false, std::nullopt };
        auto key = PlaceKey();
        view.Show(doc, key && placeOf ? placeOf(*key) : std::nullopt);
        if (changed) changed();
        if (tabsChanged) tabsChanged();
        return true;
    }

    // The shown tab goes to the background: what is on the surface is brought into the model first (only a document
    // with changes differs from the model), the unsaved state and the position stay with the tab.
    bool DocumentSession::Park()
    {
        if (!doc) return true;
        EndTextEditing();
        bool modified = IsModified();
        if (modified)
        {
            try
            {
                view.CaptureInk([] {});
            }
            catch (...)
            {
                LogException("session: bringing the ink into the document before switching tabs");
                Fail(std::format(L"Der Tab kann nicht gewechselt werden: {}", ErrorText()));
                return false;
            }
        }
        if (leaving) leaving();
        tabs[active] = { doc, openedFrom, modified, surface.CurrentPlace() };
        return true;
    }

    void DocumentSession::Switch(size_t index)
    {
        active = index;
        Tab const& tab = tabs[index];
        doc = tab.doc;
        openedFrom = tab.openedFrom;
        ++shownCount;
        changedOutsideInk = tab.modified; // unsaved until saved; the surface's history starts over
        pagesChanged = false;
        auto key = PlaceKey();
        view.Show(doc, tab.place ? tab.place : key && placeOf ? placeOf(*key) : std::nullopt);
        if (changed) changed();
        if (tabsChanged) tabsChanged();
    }

    bool DocumentSession::Activate(int index)
    {
        if (index < 0 || index >= TabCount()) return false;
        if (static_cast<size_t>(index) == active) return true;
        if (!Park()) return false;
        Switch(static_cast<size_t>(index));
        return true;
    }

    bool DocumentSession::CloseTab(int index)
    {
        if (index < 0 || index >= TabCount()) return false;
        if (!TabModified(index))
        {
            DropTab(index); // nothing to ask: the shown tab stays
            return true;
        }
        QuestionScope question(*this);
        auto closing = tabs[static_cast<size_t>(index)].doc;
        auto before = doc;
        if (!Activate(index) || !ConfirmLeave()) return false;
        int now = TabOf(closing); // by identity: the strip may have changed while the question was open
        if (now < 0) return true;
        DropTab(now);
        if (before != closing)
        {
            if (int back = TabOf(before); back >= 0) Activate(back);
        }
        return true;
    }

    void DocumentSession::DropTab(int index)
    {
        if (index < 0 || index >= TabCount()) return;
        auto const size = static_cast<size_t>(index);
        if (size == active)
        {
            if (leaving) leaving();
            tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(size));
            doc.reset();
            if (tabs.empty())
            {
                active = 0;
                Show(XoppDocument::CreateNew(), std::nullopt, false);
                return;
            }
            Switch(std::min(size, tabs.size() - 1));
            return;
        }
        tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(size));
        if (size < active) --active;
        if (tabsChanged) tabsChanged();
    }

    void DocumentSession::MoveLastTabTo(int position)
    {
        if (position < 0 || static_cast<size_t>(position) >= tabs.size() - 1) return;
        std::rotate(tabs.begin() + position, tabs.end() - 1, tabs.end());
        active = static_cast<size_t>(position); // the new tab was the shown one
        if (tabsChanged) tabsChanged();
    }

    bool DocumentSession::AnyModified() const
    {
        for (int i = 0; i < TabCount(); ++i)
        {
            if (TabModified(i)) return true;
        }
        return false;
    }

    bool DocumentSession::SaveAll()
    {
        QuestionScope question(*this);
        for (int i = 0; i < TabCount(); ++i)
        {
            if (!TabModified(i)) continue;
            if (!Activate(i) || !Save()) return false;
        }
        return true;
    }

    bool DocumentSession::ConfirmLeaveAll()
    {
        QuestionScope question(*this);
        for (int i = 0; i < TabCount(); ++i)
        {
            if (!TabModified(i)) continue;
            if (!Activate(i) || !ConfirmLeave()) return false;
        }
        return true;
    }

    std::wstring DocumentSession::TabName(int index) const
    {
        return tabs.at(static_cast<size_t>(index)).doc->SuggestedName();
    }

    bool DocumentSession::TabModified(int index) const
    {
        return static_cast<size_t>(index) == active ? IsModified() : tabs.at(static_cast<size_t>(index)).modified;
    }

    std::optional<std::filesystem::path> DocumentSession::TabFile(int index) const
    {
        auto const& tab = tabs.at(static_cast<size_t>(index));
        if (tab.doc->FilePath()) return tab.doc->FilePath();
        return tab.doc->PdfPath() ? tab.doc->PdfPath() : tab.openedFrom;
    }

    std::vector<std::shared_ptr<XoppDocument>> DocumentSession::Documents() const
    {
        std::vector<std::shared_ptr<XoppDocument>> all;
        for (auto const& tab : tabs) all.push_back(tab.doc);
        return all;
    }

    int DocumentSession::TabOf(std::shared_ptr<XoppDocument> const& document) const
    {
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            if (tabs[i].doc == document) return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<int> DocumentSession::TabsUsing(std::filesystem::path const& item) const
    {
        std::vector<int> found;
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            if (tabs[i].doc->Uses(item)) found.push_back(static_cast<int>(i));
        }
        return found;
    }

    int DocumentSession::TabWithFile(std::filesystem::path const& file) const
    {
        for (int i = 0; i < TabCount(); ++i)
        {
            auto own = TabFile(i);
            if (own && project::ProjectFiles::SamePath(file, own)) return i;
        }
        return -1;
    }

    bool DocumentSession::IsPristine() const { return doc && IsPristineTab(active); }

    bool DocumentSession::IsPristineTab(size_t index) const
    {
        auto const& tab = tabs[index];
        return !tab.doc->FilePath() && !tab.doc->PdfPath() && !tab.openedFrom && !TabModified(static_cast<int>(index));
    }

    // A tab that is not shown: the strip gets it, the surface does not change.
    void DocumentSession::InsertParked(std::shared_ptr<XoppDocument> document, std::optional<std::filesystem::path> from, int position)
    {
        size_t at = position >= 0 && static_cast<size_t>(position) <= tabs.size() ? static_cast<size_t>(position) : tabs.size();
        tabs.insert(tabs.begin() + static_cast<std::ptrdiff_t>(at), { std::move(document), std::move(from), false, std::nullopt });
        if (at <= active) ++active;
        if (tabsChanged) tabsChanged();
    }

    std::optional<std::filesystem::path> DocumentSession::PlaceKey() const
    {
        if (!doc) return std::nullopt;
        if (auto own = doc->FilePath()) return own;
        return openedFrom;
    }

    void DocumentSession::New()
    {
        if (loading)
        {
            if (message) message(L"Ein Dokument wird gerade geöffnet; bitte einen Moment warten.", MessageKind::Info);
            return;
        }
        Show(XoppDocument::CreateNew(), std::nullopt, true);
    }

    void DocumentSession::Open()
    {
        std::optional<std::filesystem::path> folder;
        if (auto file = FilePath()) folder = file->parent_path();
        auto file = Dialogs::OpenDocument(owner, folder);
        if (file) Open(*file);
    }

    void DocumentSession::Open(std::filesystem::path const& file, int position)
    {
        if (int tab = TabWithFile(file); tab >= 0)
        {
            Activate(tab);
            return;
        }
        if (refusedWhileSyncing && refusedWhileSyncing(L"Öffnen")) return;
        if (loading)
        {
            pendingOpens.emplace_back(file, position); // opened when the one loading is shown
            return;
        }
        EndTextEditing();
        Load(file, position, IsPristine() ? doc : nullptr);
    }

    void DocumentSession::OpenPending()
    {
        while (!loading && !pendingOpens.empty())
        {
            auto [file, position] = std::move(pendingOpens.front());
            pendingOpens.pop_front();
            Open(file, position); // may only show a tab that has the file: the next one follows at once
        }
    }

    bool DocumentSession::ShowRecovered(std::shared_ptr<XoppDocument> restored)
    {
        if (!Show(std::move(restored), std::nullopt, true)) return false;
        changedOutsideInk = true; // unsaved until the user saves it
        tabs[active].modified = true;
        if (changed) changed();
        if (tabsChanged) tabsChanged();
        return true;
    }

    void DocumentSession::MoveFiles(std::filesystem::path const& item, std::filesystem::path const& renamed)
    {
        if (!doc) return;
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            auto const& tabDoc = tabs[i].doc;
            if (!tabDoc->Uses(item)) continue;
            if (project::ProjectFiles::IsAtOrBelow(tabDoc->FilePath(), item))
            {
                tabDoc->SetFilePath(project::ProjectFiles::MapRenamed(*tabDoc->FilePath(), item, renamed));
            }
            if (project::ProjectFiles::IsAtOrBelow(tabDoc->PdfPath(), item))
            {
                tabDoc->MovePdf(project::ProjectFiles::MapRenamed(*tabDoc->PdfPath(), item, renamed));
            }
            for (auto const& page : tabDoc->Pages())
            {
                if (project::ProjectFiles::IsAtOrBelow(page->BackgroundImagePath(), item))
                {
                    page->SetBackgroundImagePath(project::ProjectFiles::MapRenamed(*page->BackgroundImagePath(), item, renamed));
                }
            }
            tabs[i].modified = true;
            if (i == active)
            {
                changedOutsideInk = true;
                ++edits;
            }
        }
        if (changed) changed();
        if (tabsChanged) tabsChanged();
    }

    bool DocumentSession::SaveCopy()
    {
        QuestionScope question(*this);
        if (!doc) return false;
        std::filesystem::path folder = doc->FilePath() ? doc->FilePath()->parent_path() : std::filesystem::path();
        auto file = Dialogs::SaveDocument(owner, folder, doc->SuggestedName() + L" (Kopie)");
        if (!file) return false;
        try
        {
            view.CaptureInk([&] { io::DocumentFile::SaveCopy(*doc, *file); });
            if (message) message(std::format(L"Kopie gespeichert: {}", file->filename().native()), MessageKind::Success);
            return true;
        }
        catch (...)
        {
            LogException(std::format("session: saving a copy at {}", ToUtf8(file->native())));
            Fail(std::format(L"Speichern fehlgeschlagen: {}", ErrorText()));
            return false;
        }
    }

    void DocumentSession::Load(std::filesystem::path const& file, int position, std::shared_ptr<XoppDocument> replacing)
    {
        if (loading || (refusedWhileSyncing && refusedWhileSyncing(L"Öffnen"))) return;
        loading = true;
        Log(std::format("session: opening {}", ToUtf8(file.native())));
        UiQueue::RunInBackground("open document", [this, file, position, replacing]
        {
            std::shared_ptr<XoppDocument> loaded;
            std::exception_ptr error;
            try
            {
                loaded = IsPdf(file) ? std::shared_ptr<XoppDocument>(XoppDocument::FromPdf(file, PdfPages::SizesOf(file)))
                                     : std::shared_ptr<XoppDocument>(io::DocumentFile::Load(file));
            }
            catch (...)
            {
                LogException(std::format("session: opening {}", ToUtf8(file.native())));
                error = std::current_exception();
            }
            UiQueue::Post("show document", [this, loaded, error, file, position, replacing]
            {
                loading = false;
                // Whatever ends this, the next file asked for meanwhile is opened (posted: not inside this handler).
                struct Next
                {
                    DocumentSession* session;
                    ~Next() { if (!UiQueue::Post("open the next document", [s = session] { s->OpenPending(); })) LogError("could not queue the next open"); }
                } next{ this };
                if (error)
                {
                    Dialogs::ShowError(owner, std::format(L"„{}“ kann nicht geöffnet werden.", file.filename().native()),
                        ErrorText(error));
                    if (tabs.empty() && pendingOpens.empty()) Show(XoppDocument::CreateNew(), std::nullopt, false); // a window shows a document
                    return;
                }
                // A tab of its own: whatever is shown meanwhile stays in its tab. The same file may have been opened
                // meanwhile: that tab is shown.
                if (int tab = TabWithFile(file); tab >= 0)
                {
                    if (asking == 0) Activate(tab);
                    return;
                }
                int replaced = replacing ? TabOf(replacing) : -1; // the new document it replaces, if still untouched
                if (replaced >= 0 && !IsPristineTab(static_cast<size_t>(replaced))) replaced = -1;
                int where = position >= 0 ? position : replaced;
                if (asking > 0)
                {
                    InsertParked(loaded, file, where);
                }
                else
                {
                    if (!Show(loaded, file, true)) return;
                    MoveLastTabTo(where);
                }
                if (replaced >= 0) DropTab(TabOf(replacing));
                if (fileUsed) fileUsed(file);
            });
        });
    }

    bool DocumentSession::Save()
    {
        if (!doc) return false;
        if (doc->FilePath() && saveDeferredWhileSyncing && saveDeferredWhileSyncing()) return false; // saved after the sync
        if (refusedWhileSyncing && refusedWhileSyncing(L"Speichern")) return false;
        if (!doc->FilePath()) return SaveAs();
        return SaveTo(*doc->FilePath(), false);
    }

    bool DocumentSession::SaveAs()
    {
        QuestionScope question(*this);
        if (!doc || (refusedWhileSyncing && refusedWhileSyncing(L"Speichern"))) return false;
        std::optional<std::filesystem::path> forNew = !doc->FilePath() && folderForNew ? folderForNew() : std::nullopt;
        std::filesystem::path folder = doc->FilePath() ? doc->FilePath()->parent_path()
            : forNew ? *forNew
            : doc->PdfPath() ? doc->PdfPath()->parent_path() : std::filesystem::path();
        auto file = Dialogs::SaveDocument(owner, folder, doc->SuggestedName());
        return file && SaveTo(*file, true);
    }

    bool DocumentSession::SaveTo(std::filesystem::path const& file, bool saveAs)
    {
        try
        {
            // Saving writes exactly what the ink shows now: the live pages' strokes are captured first.
            view.CaptureInk([&]
            {
                if (saveAs) io::DocumentFile::SaveAs(*doc, file);
                else io::DocumentFile::Save(*doc, file);
            });
            surface.MarkSaved();
            changedOutsideInk = false;
            pagesChanged = false;
            tabs[active].modified = false;
            if (message) message(std::format(L"Gespeichert: {}", file.filename().native()), MessageKind::Success);
            if (fileUsed) fileUsed(file);
            if (changed) changed();
            if (tabsChanged) tabsChanged();
            return true;
        }
        catch (...)
        {
            LogException(std::format("session: saving {}", ToUtf8(file.native())));
            Fail(std::format(L"Speichern fehlgeschlagen: {}", ErrorText()));
            return false;
        }
    }

    void DocumentSession::Fail(std::wstring const& what)
    {
        if (message) message(what, MessageKind::Error);
    }

    void DocumentSession::EndTextEditing()
    {
        if (!surface.EditingText()) return;
        surface.EndTextEditing(); // returns with the text in the history (AtSavePoint counts it)
        ++edits; // the history's notice comes later (posted): a load that ends now must ask about the text already
    }

    bool DocumentSession::ConfirmLeave()
    {
        QuestionScope question(*this);
        EndTextEditing(); // the typed text counts as a change (and is saved with the rest)
        if (!IsModified()) return true;
        switch (Dialogs::AskLeave(owner, doc->SuggestedName()))
        {
        case Dialogs::LeaveChoice::Save: return Save();
        case Dialogs::LeaveChoice::Discard: return true;
        default: return false;
        }
    }

    void DocumentSession::NotePagesChanged()
    {
        pagesChanged = true;
        ++edits;
        if (changed) changed();
        if (tabsChanged) tabsChanged();
    }

    bool DocumentSession::IsModified() const
    {
        // The ink's history knows whether anything changed since the last save (the model has no other edits yet).
        return doc && (changedOutsideInk || pagesChanged || !surface.AtSavePoint());
    }

    std::wstring DocumentSession::Title() const
    {
        std::wstring name = doc ? doc->SuggestedName() : L"";
        return std::format(L"{}{} – Xournal Studio", IsModified() ? L"• " : L"", name);
    }

    std::optional<std::filesystem::path> DocumentSession::FilePath() const
    {
        return doc ? doc->FilePath() : std::nullopt;
    }
}
