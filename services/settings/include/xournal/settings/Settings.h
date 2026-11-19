#pragma once

// The native app's settings: settings-native.json in the app folder, read and written with Windows.Data.Json. The C#
// app's settings.json is never read or touched. Keys this version does not know are kept (a newer version's settings
// survive an older one). Missing file: defaults. Damaged file: logged, defaults, and the file stays as it is until the
// next Save. Save writes "<file>~" first, then replaces the file.
//
// Threads: Windows.Data.Json needs an initialized COM/WinRT apartment on the calling thread (any kind).

#include <windows.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "xournal/base/Text.h"

namespace xournal::settings
{
    struct WindowState
    {
        RECT normal{};          // the restored window's rectangle (screen, pixels)
        bool maximized = true;
    };

    /// Where a document was last shown.
    struct DocumentPosition
    {
        double verticalOffset = 0;
        double zoom = 1;
        bool operator==(DocumentPosition const&) const = default;
    };

    class Settings
    {
    public:
        /// settings-native.json in the app's folder (the folder is the app's choice: %LOCALAPPDATA%\XournalStudioForWindows
        /// or its test override).
        static std::filesystem::path FileIn(std::filesystem::path const& appFolder);

        static Settings Load(std::filesystem::path file);

        /// Throws xournal::Error when the file cannot be written (the caller decides whether that matters).
        void Save() const;

        std::filesystem::path const& File() const { return file; }

        std::optional<WindowState> window;
        std::optional<std::filesystem::path> lastFile;
        /// The folder shown in the explorer (a git repository or any folder).
        std::optional<std::filesystem::path> projectFolder;
        bool paneOpen = false;
        int paneTab = 0;
        bool doneTasksShown = false;
        /// InkPages' ToolChoices::Serialize, kept as it is.
        std::string toolChoices;
        /// The last position of each opened document, by path (case ignored like Windows).
        std::map<std::wstring, DocumentPosition, LessIgnoreCase> documentPositions;

    private:
        std::filesystem::path file;
        std::wstring readJson; // the object as read, so keys this version does not know are written back
    };
}
