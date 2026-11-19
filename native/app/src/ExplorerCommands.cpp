#include "ExplorerCommands.h"

#include "ErrorText.h"
#include "Questions.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/git/GitSync.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/project/ProjectFiles.h"

#include <shellapi.h>
#include <shlobj.h>

#include <format>
#include <memory>

namespace xournal::app
{
    using project::ProjectFiles::IsAtOrBelow;

    namespace
    {
        bool IsDocument(std::filesystem::path const& file)
        {
            auto extension = file.extension().native();
            return EqualsIgnoreCase(extension, L".xopp") || EqualsIgnoreCase(extension, L".moj") || EqualsIgnoreCase(extension, L".pdf");
        }

        std::wstring NameOf(std::filesystem::path const& item)
        {
            std::wstring text = item.native();
            while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/'))
            {
                text.pop_back();
            }
            return std::filesystem::path(text).filename().native();
        }

        std::wstring FailureTitle(ExplorerCommand command)
        {
            switch (command)
            {
            case ExplorerCommand::NewDocument: return L"Dokument konnte nicht erstellt werden";
            case ExplorerCommand::NewFolder: return L"Ordner konnte nicht erstellt werden";
            case ExplorerCommand::Rename: return L"Umbenennen fehlgeschlagen";
            case ExplorerCommand::Discard: return L"Änderungen konnten nicht verworfen werden";
            case ExplorerCommand::Delete: return L"Löschen fehlgeschlagen";
            case ExplorerCommand::ShowInExplorer: return L"Im Explorer anzeigen fehlgeschlagen";
            case ExplorerCommand::CopyPath: return L"Kopieren fehlgeschlagen";
            default: return L"Öffnen fehlgeschlagen";
            }
        }
    }

    ExplorerCommands::ExplorerCommands(HWND window, DocumentSession& documentSession, SyncController& syncController,
        ProjectExplorer& projectExplorer, ExplorerTree& explorerTree, Messages show)
        : owner(window), session(documentSession), sync(syncController), explorer(projectExplorer), tree(explorerTree),
          messages(std::move(show))
    {
    }

    void ExplorerCommands::Run(ExplorerRequest const& request)
    {
        if (request.command == ExplorerCommand::Rename)
        {
            // From the label edit: carried out once the edit control is gone (the refresh rebuilds the tree).
            UiQueue::Post("explorer: rename", [this, request]
            {
                if (sync.RefusedWhileSyncing(L"Das Ändern von Dateien und Ordnern")) return;
                try
                {
                    Rename(request.path, request.isFolder, request.newName);
                }
                catch (...)
                {
                    LogException(std::format("explorer: rename {}", ToUtf8(request.path.native())));
                    messages(std::format(L"{}: {}", FailureTitle(request.command), ErrorText()), ToastKind::Error);
                }
                explorer.RefreshNow();
            });
            return;
        }
        bool changesFiles = request.command != ExplorerCommand::Open && request.command != ExplorerCommand::ShowInExplorer &&
            request.command != ExplorerCommand::CopyPath;
        if (changesFiles && sync.RefusedWhileSyncing(L"Das Ändern von Dateien und Ordnern"))
        {
            return;
        }
        std::filesystem::path folder = request.isFolder ? request.path : request.path.parent_path();
        try
        {
            switch (request.command)
            {
            case ExplorerCommand::Open: Open(request.path); break;
            case ExplorerCommand::NewDocument: NewDocument(folder); break;
            case ExplorerCommand::NewFolder: NewFolder(folder); break;
            case ExplorerCommand::Delete: Delete(request.path, request.isFolder); break;
            case ExplorerCommand::Discard: Discard(request.path); break;
            case ExplorerCommand::ShowInExplorer: ShowInExplorer(request.path); break;
            case ExplorerCommand::CopyPath: CopyPath(request.path); break;
            case ExplorerCommand::Rename: break; // above
            }
        }
        catch (...)
        {
            LogException(std::format("explorer: command {} on {}", static_cast<int>(request.command), ToUtf8(request.path.native())));
            messages(std::format(L"{}: {}", FailureTitle(request.command), ErrorText()), ToastKind::Error);
        }
        if (changesFiles)
        {
            explorer.RefreshNow();
        }
    }

    void ExplorerCommands::DocumentShown()
    {
        auto doc = session.Document();
        auto open = doc ? (doc->FilePath() ? doc->FilePath() : doc->PdfPath()) : std::nullopt;
        if (pendingPage && project::ProjectFiles::SamePath(pendingPage->first, open))
        {
            if (scrollToPage) scrollToPage(pendingPage->second);
            pendingPage.reset();
        }
    }

    void ExplorerCommands::Open(std::filesystem::path const& file, int page)
    {
        if (!Paths::FileExists(file))
        {
            messages(std::format(L"Nicht gefunden: {}", file.native()), ToastKind::Warning);
            return;
        }
        if (IsDocument(file))
        {
            auto doc = session.Document();
            auto open = doc ? (doc->FilePath() ? doc->FilePath() : doc->PdfPath()) : std::nullopt;
            pendingPage.reset();
            if (!project::ProjectFiles::SamePath(file, open))
            {
                if (page >= 0) pendingPage = { file, page };
                session.Open(file);
            }
            else if (page >= 0 && scrollToPage)
            {
                scrollToPage(page);
            }
            if (focusDocument) focusDocument(); // Entf is meant for the document then, not for the tree
            return;
        }
        // Other files open in their Windows app, as in File Explorer.
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOASYNC;
        info.hwnd = owner;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info))
        {
            ThrowLastError(std::format("explorer: open {}", ToUtf8(file.native())));
        }
    }

    std::optional<std::wstring> ExplorerCommands::NameError(std::filesystem::path const& folder, std::wstring const& name,
        std::filesystem::path const& current) const
    {
        if (auto error = project::ProjectFiles::ValidateName(name))
        {
            return error;
        }
        std::filesystem::path p = folder / name;
        std::error_code ignored; // only "does it exist"; a folder that cannot be read fails at the rename itself
        if (std::filesystem::exists(p, ignored) && !project::ProjectFiles::SamePath(p, current))
        {
            return std::format(L"In diesem Ordner gibt es „{}“ schon.", name);
        }
        return std::nullopt;
    }

    // A new, empty document in the folder, named like Xournal++ (date and time), opened, and its name ready to edit
    // (as File Explorer's "Neu" does).
    void ExplorerCommands::NewDocument(std::filesystem::path const& folder)
    {
        if (!session.ConfirmLeave())
        {
            return;
        }
        std::wstring name = project::ProjectFiles::UniqueName(folder, DocumentNames::DefaultName(DateTime::Now()),
            project::ProjectFiles::DocumentExtension);
        std::filesystem::path file = folder / name;
        auto doc = XoppDocument::CreateNew();
        io::DocumentFile::Save(*doc, file);
        session.Load(file);
        explorer.RefreshNow();
        tree.BeginRename(file);
    }

    void ExplorerCommands::NewFolder(std::filesystem::path const& folder)
    {
        auto created = project::ProjectFiles::CreateFolder(folder, project::ProjectFiles::UniqueName(folder, L"Neuer Ordner"));
        explorer.RefreshNow();
        tree.BeginRename(created);
    }

    // Exactly this file (only the name before the extension is edited) or folder - nothing else is renamed with it. The
    // open document, if it is renamed (or lies in the folder), is left (leave question) and opened again from its new
    // place; if its background PDF is renamed, the document refers to the new name.
    void ExplorerCommands::Rename(std::filesystem::path const& item, bool isFolder, std::wstring const& edited)
    {
        std::wstring newName = project::ProjectFiles::NewName(item, isFolder, std::wstring(Trim(edited)));
        if (newName == NameOf(item))
        {
            return;
        }
        std::filesystem::path parent = Paths::Full(item).parent_path();
        if (auto error = NameError(parent, newName, item))
        {
            messages(*error, ToastKind::Warning);
            return;
        }
        auto doc = session.Document();
        auto openFile = doc ? doc->FilePath() : std::nullopt;
        bool fileAffected = IsAtOrBelow(openFile, item);
        if (fileAffected && !session.ConfirmLeave())
        {
            return;
        }
        auto renamed = project::ProjectFiles::Rename(item, newName);
        tree.Select(renamed);
        if (fileAffected)
        {
            session.Load(project::ProjectFiles::MapRenamed(*openFile, item, renamed));
        }
        else if (doc && IsAtOrBelow(doc->PdfPath(), item))
        {
            session.MovePdf(project::ProjectFiles::MapRenamed(*doc->PdfPath(), item, renamed));
        }
    }

    // Exactly this file or folder to the recycle bin, after asking. If the open document uses it (its file, PDF or a
    // picture background, or a folder holding one), it is left first (leave question).
    void ExplorerCommands::Delete(std::filesystem::path const& item, bool isFolder)
    {
        std::wstring name = NameOf(item);
        if (!Questions::Confirm(owner, std::format(L"„{}“ löschen?", name), isFolder
                ? L"Der Ordner wird mit allem, was darin ist, in den Papierkorb verschoben."
                : L"Die Datei wird in den Papierkorb verschoben.", L"Löschen"))
        {
            return;
        }
        auto doc = session.Document();
        if (doc && doc->Uses(item))
        {
            if (!session.ConfirmLeave())
            {
                return;
            }
            session.ShowNew();
        }
        project::ProjectFiles::Recycle(item, owner);
    }

    // Gives exactly this file its committed content back (git), after asking. If the open document shows it, its unsaved
    // changes go too (said in the question; no save question - saving would only be overwritten) and it is opened again
    // from the restored files.
    void ExplorerCommands::Discard(std::filesystem::path const& file)
    {
        auto root = git::GitSync::FindRoot(file);
        if (!root)
        {
            return; // only modified files offer it, and those lie in a repository
        }
        auto doc = session.Document();
        bool affected = doc && doc->Uses(file);
        auto open = doc ? (doc->FilePath() ? doc->FilePath() : doc->PdfPath()) : std::nullopt;
        std::wstring text = L"Die Datei bekommt wieder den Stand des letzten Commits (der letzten Synchronisierung). Was "
            L"seitdem an ihr geändert wurde, geht verloren.";
        if (affected && session.IsModified())
        {
            text += L" Auch die ungespeicherten Änderungen im offenen Dokument gehen verloren.";
        }
        if (!Questions::Confirm(owner, std::format(L"Änderungen an „{}“ verwerfen?", NameOf(file)), text, L"Verwerfen"))
        {
            return;
        }
        if (affected)
        {
            // Left before the checkout: nothing can save it over the restored file meanwhile, and if opening it again
            // fails, the old state is not left open to be saved later.
            session.ShowNew();
        }
        git::GitSync::Discard(*root, file);
        if (affected && open)
        {
            session.Load(*open);
        }
    }

    void ExplorerCommands::ShowInExplorer(std::filesystem::path const& item)
    {
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(Paths::Full(item).c_str());
        if (!pidl)
        {
            ThrowHr(std::format("ILCreateFromPath {}", ToUtf8(item.native())), E_FAIL);
        }
        struct Owned
        {
            PIDLIST_ABSOLUTE pidl;
            ~Owned() { ILFree(pidl); }
        } owned{ pidl };
        Check("SHOpenFolderAndSelectItems", SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0));
    }

    // The path in quotes, as File Explorer's "Als Pfad kopieren" gives it.
    void ExplorerCommands::CopyPath(std::filesystem::path const& item)
    {
        std::wstring text = L"\"" + Paths::Full(item).native() + L"\"";
        if (!OpenClipboard(owner))
        {
            ThrowLastError("OpenClipboard");
        }
        struct Closer { ~Closer() { CloseClipboard(); } } closer;
        if (!EmptyClipboard())
        {
            ThrowLastError("EmptyClipboard");
        }
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory)
        {
            ThrowLastError("GlobalAlloc");
        }
        void* target = GlobalLock(memory);
        if (!target)
        {
            GlobalFree(memory);
            ThrowLastError("GlobalLock");
        }
        memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory))
        {
            GlobalFree(memory);
            ThrowLastError("SetClipboardData");
        }
        messages(L"Pfad kopiert.", ToastKind::Success);
    }
}
