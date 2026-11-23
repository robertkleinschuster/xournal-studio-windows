#include "SyncController.h"

#include "ErrorText.h"
#include "Questions.h"
#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/git/GitCredentials.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        std::wstring MachineName()
        {
            wchar_t name[MAX_COMPUTERNAME_LENGTH + 1]{};
            DWORD size = ARRAYSIZE(name);
            if (!GetComputerNameW(name, &size))
            {
                LogError(std::format("sync: GetComputerName: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                return L"Windows";
            }
            return name;
        }

        std::wstring Describe(git::SyncResult const& result, bool pullOnly)
        {
            if (pullOnly)
            {
                return result.pulledFiles.empty() ? L"Schon aktuell: der Server hat nichts Neues." :
                    result.pulledFiles.size() == 1 ? L"Vom Server geholt: 1 Datei." :
                    std::format(L"Vom Server geholt: {} Dateien.", result.pulledFiles.size());
            }
            std::wstring text;
            if (!result.pulledFiles.empty())
            {
                text = result.pulledFiles.size() == 1 ? L"1 Datei geholt" : std::format(L"{} Dateien geholt", result.pulledFiles.size());
            }
            if (result.pushed)
            {
                text += (text.empty() ? L"" : L", ") + std::wstring(result.committed ? L"Änderungen hochgeladen" : L"Commits hochgeladen");
            }
            return text.empty() ? L"Synchronisiert: alles war schon aktuell." : L"Synchronisiert: " + text + L".";
        }
    }

    SyncController::SyncController(HWND window, DocumentSession& documentSession, Messages show)
        : owner(window), session(documentSession), messages(std::move(show))
    {
    }

    bool SyncController::RefusedWhileSyncing(std::wstring const& what) const
    {
        if (syncing)
        {
            messages(std::format(L"Synchronisierung läuft: {} ist möglich, sobald sie fertig ist.", what), MessageKind::Info);
        }
        return syncing;
    }

    bool SyncController::DeferSave()
    {
        if (!syncing)
        {
            return false;
        }
        saveRequested = true;
        messages(L"Synchronisierung läuft: gespeichert wird, sobald sie fertig ist.", MessageKind::Info);
        return true;
    }

    // Saving was asked for during the sync and the pull did not change the document.
    void SyncController::SaveIfRequested()
    {
        bool wanted = saveRequested;
        saveRequested = false;
        auto doc = session.Document();
        if (wanted && session.IsModified() && doc && doc->FilePath())
        {
            session.Save();
        }
    }

    void SyncController::SetSyncing(bool value)
    {
        syncing = value;
        if (stateChanged) stateChanged();
    }

    void SyncController::Sync(std::optional<std::filesystem::path> const& folder) { Run(folder, false); }

    void SyncController::Pull(std::optional<std::filesystem::path> const& folder) { Run(folder, true); }

    void SyncController::Run(std::optional<std::filesystem::path> const& folder, bool pullOnly)
    {
        if (syncing)
        {
            return;
        }
        if (!folder)
        {
            messages(L"Kein Projektordner geöffnet (Datei → Ordner öffnen).", MessageKind::Info);
            return;
        }
        std::optional<std::filesystem::path> root;
        try
        {
            root = git::GitSync::FindRoot(*folder);
        }
        catch (...)
        {
            LogException("sync: find repository");
            messages(std::format(L"Synchronisieren fehlgeschlagen: {}", ErrorText()), MessageKind::Error);
            return;
        }
        if (!root)
        {
            messages(L"Der Projektordner ist kein Git-Repository.", MessageKind::Info);
            return;
        }
        session.EndTextEditing(); // a text being typed counts as a change
        // Pulling uploads nothing: the open document's unsaved notes are asked about only if the pull changes it.
        if (!pullOnly && session.IsModified())
        {
            int choice = Questions::Choose(owner, L"Vor dem Synchronisieren speichern?",
                L"Hochgeladen wird nur, was gespeichert ist. Ohne Speichern bleiben die Änderungen ungespeichert im Fenster.",
                { L"Speichern", L"Nicht speichern", L"Abbrechen" });
            if (choice == 0 && !session.Save())
            {
                return;
            }
            if (choice != 0 && choice != 1)
            {
                return;
            }
        }
        SetSyncing(true);
        Log(std::format("sync: {}", ToUtf8(root->native())));
        UiQueue::RunInBackground(pullOnly ? "pull" : "sync", [this, pullOnly, root = *root, machine = MachineName()]
        {
            std::optional<git::SyncResult> result;
            std::exception_ptr error;
            try
            {
                result = pullOnly ? git::GitSync::Pull(root, git::GitCredentials::FromCredentialManager(), git::Moment::Now())
                    : git::GitSync::Sync(root, git::GitCredentials::FromCredentialManager(), machine, git::Moment::Now());
            }
            catch (...)
            {
                error = std::current_exception();
            }
            UiQueue::Post("sync finished", [this, pullOnly, result, error]
            {
                SetSyncing(false);
                if (finished) finished(!error);
                if (!error)
                {
                    Finish(*result, pullOnly);
                    SaveIfRequested();
                    return;
                }
                SaveIfRequested(); // the sync failed: the files are as they were
                try
                {
                    std::rethrow_exception(error);
                }
                catch (git::SyncConflict const& conflict)
                {
                    messages(std::format(L"Konflikt beim {}: {}. Die eigenen Änderungen sind committet, aber "
                        L"nicht hochgeladen; den Konflikt mit Git lösen.", pullOnly ? L"Holen" : L"Synchronisieren",
                        FromUtf8(conflict.what())), MessageKind::Error);
                }
                catch (...)
                {
                    messages(std::format(L"{} fehlgeschlagen: {}", pullOnly ? L"Holen" : L"Synchronisieren", ErrorText()),
                        MessageKind::Error);
                }
            });
        });
    }

    void SyncController::Finish(git::SyncResult const& result, bool pullOnly)
    {
        auto doc = session.Document();
        bool documentChanged = false;
        for (auto const& file : result.pulledFiles)
        {
            documentChanged = documentChanged || (doc && doc->Uses(file)); // also a picture background it shows
        }
        if (!documentChanged || ResolvePulledDocument())
        {
            messages(Describe(result, pullOnly), MessageKind::Success);
        }
    }

    bool SyncController::ResolvePulledDocument()
    {
        saveRequested = false; // asked for before the document changed on disk: the user decides again
        session.EndTextEditing(); // a text typed during the sync is one of the unsaved notes
        if (session.IsModified())
        {
            int choice = Questions::Choose(owner, L"Dokument wurde auf einem anderen Gerät geändert",
                L"Die Synchronisierung hat das offene Dokument geändert, während es neue, nicht gespeicherte Notizen hatte.",
                { L"Neu laden (meine Notizen verwerfen)", L"Meine als Kopie speichern…", L"Meine behalten" }, 1);
            if (choice == 2 || choice < 0)
            {
                messages(L"Eigene Version behalten: die geholte Version wird beim Speichern mit der eigenen überschrieben; "
                    L"sie bleibt in der Git-Historie.", MessageKind::Warning);
                return false;
            }
            if (choice == 1 && !session.SaveCopy())
            {
                messages(L"Keine Kopie gespeichert: die geholte Version wird beim Speichern mit der eigenen überschrieben; "
                    L"sie bleibt in der Git-Historie.", MessageKind::Warning);
                return false;
            }
        }
        auto doc = session.Document();
        if (doc->FilePath() && Paths::FileExists(*doc->FilePath()))
        {
            session.Load(*doc->FilePath());
            return true;
        }
        if (doc->PdfPath() && Paths::FileExists(*doc->PdfPath()))
        {
            session.Load(*doc->PdfPath());
            return true;
        }
        auto gone = doc->FilePath();
        session.ShowNew();
        messages(std::format(L"Das Dokument wurde auf einem anderen Gerät gelöscht oder umbenannt: {} gibt es nach der "
            L"Synchronisierung nicht mehr.", gone ? gone->native() : std::wstring()), MessageKind::Warning);
        return false;
    }
}
