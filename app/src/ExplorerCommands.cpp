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
#include "xournal/project/FileClipboard.h"
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
            case ExplorerCommand::Cut: return L"Ausschneiden fehlgeschlagen";
            case ExplorerCommand::Copy: return L"Kopieren fehlgeschlagen";
            case ExplorerCommand::Paste: return L"Einfügen fehlgeschlagen";
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
            request.command != ExplorerCommand::CopyPath && request.command != ExplorerCommand::Cut &&
            request.command != ExplorerCommand::Copy;
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
            case ExplorerCommand::Cut: PutOnClipboard(request.path, true); break;
            case ExplorerCommand::Copy: PutOnClipboard(request.path, false); break;
            case ExplorerCommand::Paste: Paste(folder); break;
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
        // After Run's own refresh (it rebuilds the tree, which would end the edit).
        UiQueue::Post("explorer: name the new document", [this, file] { tree.BeginRename(file); });
    }

    void ExplorerCommands::NewFolder(std::filesystem::path const& folder)
    {
        auto created = project::ProjectFiles::CreateFolder(folder, project::ProjectFiles::UniqueName(folder, L"Neuer Ordner"));
        explorer.RefreshNow();
        UiQueue::Post("explorer: name the new folder", [this, created] { tree.BeginRename(created); });
    }

    // Exactly this file (only the name before the extension is edited) or folder - nothing else is renamed with it. The
    // open document, if it is renamed (or lies in the folder), is left (leave question) and opened again from its new
    // place; if a file it shows is renamed (its PDF, a picture background, a folder holding them), the document refers
    // to the new name and counts as changed (saved by choice, as always).
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
        else if (doc && doc->Uses(item))
        {
            session.MoveFiles(item, renamed); // its PDF or a picture background (or a folder holding them)
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

    // "Änderungen verwerfen" of a file or a folder with everything in it, after asking (naming the files): files of the
    // last commit get its content back or come back when deleted (git) - their current versions go to the recycle bin
    // as copies -, new files go to the recycle bin. If the open document uses one of them, its unsaved changes go too
    // (said in the question; no save question - saving would only be overwritten) and it is opened again from the
    // restored files.
    void ExplorerCommands::Discard(std::filesystem::path const& item)
    {
        auto root = tree.Snapshot().gitRoot; // a deleted item (or its folder) is not on disk to look from
        if (!root)
        {
            return; // only items with changes offer it, and those lie in a repository
        }
        git::DiscardPlan plan = git::GitSync::PlanDiscard(*root, item);
        if (plan.Empty())
        {
            messages(std::format(L"„{}“ hat keine Änderungen.", NameOf(item)), ToastKind::Info);
            return;
        }
        auto doc = session.Document();
        auto open = doc ? (doc->FilePath() ? doc->FilePath() : doc->PdfPath()) : std::nullopt;
        bool affected = false;
        for (auto const* files : { &plan.restore, &plan.remove, &plan.unstage })
        {
            for (auto const& file : *files) affected = affected || (doc && doc->Uses(file));
        }
        std::wstring text;
        auto list = [&text](std::vector<std::filesystem::path> const& files, wchar_t const* heading)
        {
            if (files.empty()) return;
            text += std::format(L"{}{}\n", text.empty() ? L"" : L"\n", heading);
            constexpr size_t Shown = 8;
            for (size_t i = 0; i < files.size() && i < Shown; i++)
            {
                text += L"  " + NameOf(files[i]) + L"\n";
            }
            if (files.size() > Shown)
            {
                text += std::format(L"  … und {} weitere\n", files.size() - Shown);
            }
        };
        list(plan.restore, L"Bekommen den Stand des letzten Commits (der letzten Synchronisierung) zurück:");
        if (!plan.overwritten.empty())
        {
            text += L"Die jetzigen Fassungen kommen in den Papierkorb.\n";
        }
        list(plan.remove, L"Neu seit dem letzten Commit – kommen in den Papierkorb:");
        if (affected) session.EndTextEditing(); // a text being typed counts among the unsaved changes
        if (affected && session.IsModified())
        {
            text += L"\nAuch die ungespeicherten Änderungen im offenen Dokument gehen verloren.";
        }
        if (!Questions::Confirm(owner, std::format(L"Änderungen an „{}“ verwerfen?", NameOf(item)), text, L"Verwerfen"))
        {
            return;
        }
        // Files first, the open document last: whatever fails (a copy, the bin declined or a locked file, git) throws
        // before the document is touched - it stays open with its changes. Each step only hands content to the bin
        // before the next one overwrites or removes it, so nothing is lost on the way.
        project::ProjectFiles::RecycleCopies(plan.overwritten, owner); // the current versions, to take back from the bin
        project::ProjectFiles::Recycle(plan.remove, owner);
        git::GitSync::Discard(*root, plan);
        if (affected)
        {
            // Opened again from the restored files (its unsaved changes go, as the question said).
            if (open && Paths::FileExists(*open)) session.Load(*open);
            else session.ShowNew();
        }
    }

    // Ausschneiden / Kopieren: the item on the Windows clipboard as File Explorer puts files there.
    void ExplorerCommands::PutOnClipboard(std::filesystem::path const& item, bool move)
    {
        project::FileClipboard::Put(owner, { Paths::Full(item) }, move);
        messages(move ? std::format(L"Ausgeschnitten: {} – mit Einfügen in einen Ordner verschieben.", NameOf(item))
                      : std::format(L"Kopiert: {}", NameOf(item)), ToastKind::Success);
    }

    // Einfügen: the files on the clipboard (from here or File Explorer) copied or - after Ausschneiden - moved into the
    // folder by the shell (its conflict questions, undoable in File Explorer). Moved files the open document uses (its
    // own file, its PDF, picture backgrounds, folders holding them) are followed: the document refers to them at their
    // new place and counts as changed (MoveFiles, as for a rename).
    void ExplorerCommands::Paste(std::filesystem::path const& folder)
    {
        auto content = project::FileClipboard::Get(owner);
        if (!content || content->files.empty())
        {
            messages(L"In der Zwischenablage sind keine Dateien.", ToastKind::Info);
            return;
        }
        auto done = project::ProjectFiles::Transfer(content->files, folder, content->move, owner);
        if (content->move)
        {
            for (auto const& [from, to] : done)
            {
                auto doc = session.Document();
                if (doc && doc->Uses(from))
                {
                    session.MoveFiles(from, to);
                }
            }
            if (!done.empty())
            {
                project::FileClipboard::Clear(owner); // the files are no longer where the clipboard says
            }
        }
        if (!done.empty())
        {
            tree.Select(done.front().to);
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
