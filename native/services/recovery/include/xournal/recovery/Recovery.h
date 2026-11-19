#pragma once

// Crash recovery like Xournal++'s autosave: while a document has unsaved changes, a copy of it is written to the app's
// own folder (never next to the document: the project folder is git-synced). The copy is removed once the user saved
// or discarded the changes; a copy whose session is gone (crash, power loss, Windows ended the app) is offered for
// restoring at the next start (io::DocumentFile::LoadCopy restores it).
//
// Each copy is "<id>.xopp" with "<id>.json" (what it belongs to and which process wrote it). The native app keeps them
// in "Recovery-native" next to the C# app's "Recovery" folder, so neither app ever offers, restores or deletes the
// other's copies (their info files differ, and each process id check is only meaningful for its own app).

#include "xournal/model/DocumentNames.h"
#include "xournal/model/Xml.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xournal::recovery
{
    using std::filesystem::path;
    using Time = std::chrono::file_clock::time_point; // FILETIME: UTC, 100 ns steps

    /// A process, told apart from a later one with the same id by its start time.
    struct ProcessStamp
    {
        DWORD id = 0;
        Time start{};

        static ProcessStamp Current();
        bool operator==(ProcessStamp const&) const = default;
    };

    /// What a recovery copy belongs to.
    struct RecoveryInfo
    {
        std::optional<path> documentPath; // the document's file; nothing if it was never saved
        path referenceBase;               // PDF references in the copy are relative to this (XoppDocument::ReferenceBase)
        std::optional<path> pdfPath;      // the background PDF, for the name of a document never saved
        std::optional<DateTime> createdAt;
        Time savedAt{};
        ProcessStamp process;             // the session that wrote it
    };

    /// A copy left behind by a session that ended without the user deciding to save or discard.
    struct RecoveryEntry
    {
        std::wstring id;
        RecoveryInfo info;
        path copyPath;
    };

    class Recovery
    {
    public:
        /// The native app's recovery folder in the app's folder: "<appFolder>\Recovery-native".
        static path FolderIn(path const& appFolder);

        /// A new copy id (a GUID as 32 hex digits).
        static std::wstring NewId();

        explicit Recovery(path folder) : folder(std::move(folder)) {}

        path const& Folder() const { return folder; }

        /// Writes the copy, then its info (a copy without info is never offered: a session that died while writing its
        /// first copy has nothing complete). Any thread; needs a COM/WinRT apartment (Windows.Data.Json).
        void Write(std::wstring const& id, XmlElement const& snapshot, RecoveryInfo const& info) const;

        /// Removes a session's copy (the user saved or discarded the changes); nothing written is fine.
        void Delete(std::wstring const& id) const;

        /// The user discarded a copy at the start: it goes to the recycle bin under a readable name
        /// ("<name> (Wiederherstellung 2026-11-19 14-05).xopp"), so nothing is lost for good by one click. If that
        /// fails, the copy gets its name back (it is offered again) and the error is thrown. Needs a single-threaded
        /// apartment (IFileOperation).
        void Recycle(RecoveryEntry const& entry, std::wstring const& name, HWND owner = nullptr) const;

        /// Copies whose session is no longer running, newest first. An unreadable info file is logged and left alone.
        std::vector<RecoveryEntry> FindAbandoned() const;

        /// Whether the process that wrote a copy still runs (same id and start time).
        static bool IsRunning(ProcessStamp const& process);

    private:
        path CopyPath(std::wstring const& id) const { return folder / (id + L".xopp"); }
        path InfoPath(std::wstring const& id) const { return folder / (id + L".json"); }

        path folder;
    };
}
