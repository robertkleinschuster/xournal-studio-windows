#include "DocumentSession.h"

#include "Dialogs.h"
#include "ErrorText.h"
#include "PdfPages.h"
#include "UiQueue.h"

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

    void DocumentSession::Show(std::shared_ptr<XoppDocument> next)
    {
        doc = std::move(next);
        recovered = false;
        view.Show(doc);
        if (changed) changed();
    }

    void DocumentSession::New()
    {
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
        recovered = true; // unsaved until the user saves it
        if (changed) changed();
    }

    void DocumentSession::MovePdf(std::filesystem::path const& pdf)
    {
        if (doc) doc->MovePdf(pdf);
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
        Log(std::format("session: opening {}", ToUtf8(file.native())));
        UiQueue::RunInBackground("open document", [this, file]
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
            UiQueue::Post("show document", [this, loaded, error, file]
            {
                loading = false;
                if (error)
                {
                    Dialogs::ShowError(owner, std::format(L"„{}“ kann nicht geöffnet werden.", file.filename().native()),
                        ErrorText(error));
                    return;
                }
                Show(loaded);
            });
        });
    }

    bool DocumentSession::Save()
    {
        if (!doc || (refusedWhileSyncing && refusedWhileSyncing(L"Speichern"))) return false;
        if (!doc->FilePath()) return SaveAs();
        return SaveTo(*doc->FilePath(), false);
    }

    bool DocumentSession::SaveAs()
    {
        if (!doc || (refusedWhileSyncing && refusedWhileSyncing(L"Speichern"))) return false;
        std::filesystem::path folder = doc->FilePath() ? doc->FilePath()->parent_path()
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
            recovered = false;
            if (message) message(std::format(L"Gespeichert: {}", file.filename().native()), ToastKind::Success);
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

    bool DocumentSession::ConfirmLeave()
    {
        if (!IsModified()) return true;
        switch (Dialogs::AskLeave(owner, doc->SuggestedName()))
        {
        case Dialogs::LeaveChoice::Save: return Save();
        case Dialogs::LeaveChoice::Discard: return true;
        default: return false;
        }
    }

    bool DocumentSession::IsModified() const
    {
        // The ink's history knows whether anything changed since the last save (the model has no other edits yet).
        return doc && (recovered || !surface.AtSavePoint());
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
