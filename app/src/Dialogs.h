#pragma once

// The app's dialogs: Windows' file dialogs (IFileOpenDialog / IFileSaveDialog) and task dialogs. German texts.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace xournal::app::Dialogs
{
    // Store the WM_INITDIALOG parameter for subsequent dialog messages.
    void SetState(HWND dialog, LPARAM state);
    INT_PTR ControlColor(HWND dialog, UINT message, WPARAM wp, LPARAM lp);
    bool DrawDarkPushButton(NMHDR const& header);
    void SetDarkCalendarPalette(NMHDR const& header);

    /// Öffnen: a .xopp, a MrWriter .moj or a PDF; nothing when cancelled.
    std::optional<std::filesystem::path> OpenDocument(HWND owner, std::optional<std::filesystem::path> const& folder);

    /// A PDF (e.g. the iPad import's source); nothing when cancelled.
    std::optional<std::filesystem::path> OpenPdf(HWND owner, std::optional<std::filesystem::path> const& folder, wchar_t const* title);

    /// A PDF or a .xopp whose pages are inserted; nothing when cancelled.
    std::optional<std::filesystem::path> OpenPagesSource(HWND owner, std::optional<std::filesystem::path> const& folder);

    /// A question with a warning icon: `action` (e.g. "Ersetzen", "Löschen") or Abbrechen (the default). True: action.
    bool Confirm(HWND owner, std::wstring const& instruction, std::wstring const& detail, wchar_t const* action);

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
