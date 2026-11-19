#pragma once

// The app's dialogs: Windows' file dialogs (IFileOpenDialog / IFileSaveDialog) and task dialogs. German texts.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace xournal::app::Dialogs
{
    /// Öffnen: a .xopp, a MrWriter .moj or a PDF; nothing when cancelled.
    std::optional<std::filesystem::path> OpenDocument(HWND owner, std::optional<std::filesystem::path> const& folder);

    /// Speichern unter: a .xopp in `folder` named `name`; nothing when cancelled.
    std::optional<std::filesystem::path> SaveDocument(HWND owner, std::filesystem::path const& folder, std::wstring const& name);

    /// Als PDF exportieren: a .pdf in `folder` named `name`; nothing when cancelled.
    std::optional<std::filesystem::path> SavePdf(HWND owner, std::filesystem::path const& folder, std::wstring const& name);

    /// Ordner öffnen; nothing when cancelled.
    std::optional<std::filesystem::path> PickFolder(HWND owner, std::optional<std::filesystem::path> const& folder);

    enum class LeaveChoice { Save, Discard, Cancel };

    /// The one question whenever a document with changes is left: Speichern / Nicht speichern / Abbrechen.
    LeaveChoice AskLeave(HWND owner, std::wstring const& documentName);

    /// An error the user must see before going on (e.g. a document that cannot be opened).
    void ShowError(HWND owner, std::wstring const& title, std::wstring const& detail);
}
