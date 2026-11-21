#pragma once

// The menu bar (Datei, Bearbeiten, Seite, Ansicht) with the C# app's items and keyboard shortcuts, and the accelerator
// table that carries the shortcuts. Items whose feature is not built yet are shown disabled, not hidden.

#include <windows.h>

#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace xournal::app
{
    class MenuAppearance
    {
    public:
        MenuAppearance() = default;
        ~MenuAppearance();
        MenuAppearance(MenuAppearance const&) = delete;
        MenuAppearance& operator=(MenuAppearance const&) = delete;
        void Apply(HMENU menu, bool topLevel, bool dark);
        bool Measure(HWND window, MEASUREITEMSTRUCT& item) const;
        bool Draw(HWND window, DRAWITEMSTRUCT const& item) const;
        LRESULT MenuChar(wchar_t key, HMENU menu) const;

    protected:
        void RefreshItems(HMENU menu);

    private:
        struct Entry
        {
            std::wstring text;
            bool topLevel = false;
            bool styled = false;
            UINT originalType = 0;
            ULONG_PTR originalData = 0;
        };
        void Style(HMENU menu, bool topLevel);
        void RestoreItems();
        std::map<HMENU, std::vector<Entry>> entries;
        HBRUSH darkBackground = nullptr;
        bool darkMode = false;
    };

    UINT TrackContextMenu(HMENU menu, HWND owner, POINT screen);

    class MainMenu : private MenuAppearance
    {
    public:
        MainMenu();
        ~MainMenu();
        MainMenu(MainMenu const&) = delete;
        MainMenu& operator=(MainMenu const&) = delete;

        /// The menu bar; the window takes it over (SetMenu), so it is destroyed with the window.
        HMENU Bar() const { return bar; }
        void WindowOwnsBar() { ownsBar = false; }
        HACCEL Accelerators() const { return accelerators; }

        void Enable(int command, bool enabled);
        void Check(int command, bool checked);
        /// Datei -> Zuletzt geöffnet: the files, newest first (CmdRecentFirst + index).
        void SetRecentFiles(std::vector<std::filesystem::path> const& files);
        void SetDarkMode(HWND window, bool dark);
        using MenuAppearance::Measure;
        using MenuAppearance::Draw;
        using MenuAppearance::MenuChar;

    private:
        HMENU bar = nullptr;
        bool ownsBar = true;
        HACCEL accelerators = nullptr;
        HMENU recent = nullptr; // Zuletzt geöffnet (a submenu of Datei)
    };
}
