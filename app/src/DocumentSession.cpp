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

    void DocumentSession::Show(std::shared_ptr<XoppDocument> next, std::optional<std::filesystem::path> from)
    {
        if (doc && leaving) leaving();
        doc = std::move(next);
        openedFrom = std::move(from);
        ++shownCount;
        changedOutsideInk = false;
        pagesChanged = false;
        auto key = PlaceKey();
        view.Show(doc, key && placeOf ? placeOf(*key) : std::nullopt);
        if (changed) changed();
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
            if (message) message(L"Ein Dokument wird gerade geöffnet; bitte einen Moment warten.", ToastKind::Info);
            return;
        }
        if (!ConfirmLeave()) return;
        Show(XoppDocument::CreateNew());
    }

    void DocumentSession::Open()
    {
        std::optional<std::filesystem::path> folder;
        if (auto file = FilePath()) folder = file->parent_path();
        auto file = Dialogs::OpenDocument(owner, folder);
        if (file) Open(*file);
    }

    void DocumentSession::Open(std::filesystem::path const& file)
    {
        if (loading || (refusedWhileSyncing && refusedWhileSyncing(L"Öffnen")) || !ConfirmLeave()) return;
        Load(file);
    }

    void DocumentSession::ShowNew()
    {
        Show(XoppDocument::CreateNew());
    }

    void DocumentSession::ShowRecovered(std::shared_ptr<XoppDocument> restored)
    {
        Show(std::move(restored));
        changedOutsideInk = true; // unsaved until the user saves it
        if (changed) changed();
    }

    void DocumentSession::MoveFiles(std::filesystem::path const& item, std::filesystem::path const& renamed)
    {
        if (!doc) return;
        if (project::ProjectFiles::IsAtOrBelow(doc->FilePath(), item))
        {
            doc->SetFilePath(project::ProjectFiles::MapRenamed(*doc->FilePath(), item, renamed));
        }
        if (project::ProjectFiles::IsAtOrBelow(doc->PdfPath(), item))
        {
            doc->MovePdf(project::ProjectFiles::MapRenamed(*doc->PdfPath(), item, renamed));
        }
        for (auto const& page : doc->Pages())
        {
            if (project::ProjectFiles::IsAtOrBelow(page->BackgroundImagePath(), item))
            {
                page->SetBackgroundImagePath(project::ProjectFiles::MapRenamed(*page->BackgroundImagePath(), item, renamed));
            }
        }
        changedOutsideInk = true;
        ++edits;
        if (changed) changed();
    }

    bool DocumentSession::SaveCopy()
    {
        if (!doc) return false;
        std::filesystem::path folder = doc->FilePath() ? doc->FilePath()->parent_path() : std::filesystem::path();
        auto file = Dialogs::SaveDocument(owner, folder, doc->SuggestedName() + L" (Kopie)");
        if (!file) return false;
        try
        {
            view.CaptureInk([&] { io::DocumentFile::SaveCopy(*doc, *file); });
            if (message) message(std::format(L"Kopie gespeichert: {}", file->filename().native()), ToastKind::Success);
            return true;
        }
        catch (...)
        {
            LogException(std::format("session: saving a copy at {}", ToUtf8(file->native())));
            Fail(std::format(L"Speichern fehlgeschlagen: {}", ErrorText()));
            return false;
        }
    }

    void DocumentSession::Load(std::filesystem::path const& file)
    {
        if (loading || (refusedWhileSyncing && refusedWhileSyncing(L"Öffnen"))) return;
        loading = true;
        std::uint64_t editsBefore = edits, shownBefore = shownCount;
        Log(std::format("session: opening {}", ToUtf8(file.native())));
        UiQueue::RunInBackground("open document", [this, file, editsBefore, shownBefore]
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
            UiQueue::Post("show document", [this, loaded, error, file, editsBefore, shownBefore]
            {
                loading = false;
                if (error)
                {
                    Dialogs::ShowError(owner, std::format(L"„{}“ kann nicht geöffnet werden.", file.filename().native()),
                        ErrorText(error));
                    return;
                }
                if (shownCount != shownBefore)
                {
                    // Another document was shown while this one loaded (a new or recovered one): that one stays.
                    Log(std::format("session: {} loaded after another document was shown - not shown", ToUtf8(file.native())));
                    if (message) message(std::format(L"„{}“ wurde nicht geöffnet: inzwischen wird ein anderes Dokument gezeigt.",
                        file.filename().native()), ToastKind::Warning);
                    return;
                }
                // Edits made while it loaded belong to the shown document: the question about them comes now (a text
                // still being typed is one of them).
                EndTextEditing();
                if (edits != editsBefore && !ConfirmLeave())
                {
                    Log(std::format("session: {} loaded, kept the edited document (Abbrechen)", ToUtf8(file.native())));
                    if (message) message(std::format(L"„{}“ wurde nicht geöffnet; das geänderte Dokument bleibt offen.",
                        file.filename().native()), ToastKind::Info);
                    return;
                }
                Show(loaded, file);
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
        pagesChanged = false;
            if (message) message(std::format(L"Gespeichert: {}", file.filename().native()), ToastKind::Success);
            if (fileUsed) fileUsed(file);
            if (changed) changed();
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
        if (message) message(what, ToastKind::Error);
    }

    void DocumentSession::EndTextEditing()
    {
        if (!surface.EditingText()) return;
        surface.EndTextEditing(); // returns with the text in the history (AtSavePoint counts it)
        ++edits; // the history's notice comes later (posted): a load that ends now must ask about the text already
    }

    bool DocumentSession::ConfirmLeave()
    {
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
