#pragma once

// The menu bar (Datei, Bearbeiten, Seite, Ansicht) with the C# app's items and keyboard shortcuts, and the accelerator
// table that carries the shortcuts. Items whose feature is not built yet are shown disabled, not hidden.

#include <windows.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace xournal::app
{
    class MainMenu
    {
    public:
        MainMenu();
        ~MainMenu();
        MainMenu(MainMenu const&) = delete;
        MainMenu& operator=(MainMenu const&) = delete;

        /// The menu bar; the window takes it over (SetMenu), so it is destroyed with the window.
        HMENU Bar() const { return bar; }
        HACCEL Accelerators() const { return accelerators; }

        void Enable(int command, bool enabled);
        void Check(int command, bool checked);
        /// Datei -> Zuletzt geöffnet: the files, newest first (CmdRecentFirst + index).
        void SetRecentFiles(std::vector<std::filesystem::path> const& files);

    private:
        HMENU bar = nullptr;
        HACCEL accelerators = nullptr;
        HMENU recent = nullptr; // Zuletzt geöffnet (a submenu of Datei)
    };
}
