#pragma once

// The native app's settings: settings-native.json in the app folder (the C# app's settings.json is never touched),
// read and written with Windows.Data.Json. Unknown keys are kept (a newer version's settings survive an older one).

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace xournal::app
{
    struct WindowState
    {
        RECT normal{};          // the restored window's rectangle (screen, pixels)
        bool maximized = true;
    };

    class Settings
    {
    public:
        /// Reads the file (missing: defaults; damaged: logged, defaults, and the file is left as it is until a save).
        static Settings Load(std::filesystem::path file);

        void Save() const;

        std::optional<WindowState> window;
        std::optional<std::filesystem::path> lastFile;
        std::optional<std::filesystem::path> projectFolder;
        bool paneOpen = false;
        int paneTab = 0;
        std::string toolChoices; // InkPages' ToolChoices::Serialize

    private:
        std::filesystem::path file;
        std::wstring unknownJson; // the object as read, so keys this version does not know are written back
    };
}
