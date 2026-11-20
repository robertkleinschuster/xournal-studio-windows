#include "MainMenu.h"

#include "Commands.h"
#include "xournal/base/Error.h"

#include <shlwapi.h>

#include <format>
#include <initializer_list>
#include <string>
#include <vector>

namespace xournal::app
{
    namespace
    {
        struct Item
        {
            int command = 0;           // 0: separator
            wchar_t const* text = L"";
            wchar_t const* keys = L""; // shown right-aligned after a tab
            BYTE modifiers = 0;        // FCONTROL, FSHIFT (FVIRTKEY is added)
            WORD key = 0;              // virtual key; 0: no shortcut
            bool enabled = true;
        };

        HMENU Popup(std::initializer_list<Item> items, std::vector<ACCEL>& accelerators)
        {
            HMENU menu = CreatePopupMenu();
            if (!menu)
            {
                ThrowLastError("CreatePopupMenu");
            }
            for (auto const& item : items)
            {
                if (item.command == 0)
                {
                    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                    continue;
                }
                std::wstring text = item.text;
                if (*item.keys)
                {
                    text += L"\t";
                    text += item.keys;
                }
                UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED);
                if (!AppendMenuW(menu, flags, static_cast<UINT_PTR>(item.command), text.c_str()))
                {
                    ThrowLastError("AppendMenu");
                }
                if (item.key)
                {
                    accelerators.push_back({ static_cast<BYTE>(FVIRTKEY | item.modifiers), item.key, static_cast<WORD>(item.command) });
                }
            }
            return menu;
        }

        void AddPopup(HMENU bar, HMENU popup, wchar_t const* title)
        {
            if (!AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), title))
            {
                ThrowLastError("AppendMenu (popup)");
            }
        }
    }

    MainMenu::MainMenu()
    {
        std::vector<ACCEL> keys;
        bar = CreateMenu();
        if (!bar)
        {
            ThrowLastError("CreateMenu");
        }
        constexpr BYTE Ctrl = FCONTROL, CtrlShift = FCONTROL | FSHIFT;
        // Features of later phases are listed (as in the C# app) but disabled until they exist.
        AddPopup(bar, Popup({
            { CmdNew, L"&Neu", L"Strg+N", Ctrl, 'N' },
            { CmdOpen, L"Ö&ffnen…", L"Strg+O", Ctrl, 'O' },
            { CmdSave, L"&Speichern", L"Strg+S", Ctrl, 'S' },
            { CmdSaveAs, L"Speichern &unter…", L"Strg+Umschalt+S", CtrlShift, 'S' },
            { CmdExportPdf, L"Als PDF e&xportieren…", L"Strg+E", Ctrl, 'E' },
            { CmdImportPdf, L"PDF mit iPad-Anmerkungen &importieren…" },
            {},
            { CmdRecentFirst, L"Zuletzt &geöffnet" }, // replaced by the submenu below
            {},
            { CmdOpenFolder, L"O&rdner öffnen…" },
            { CmdSync, L"S&ynchronisieren" },
            {},
            { CmdExit, L"&Beenden", L"Alt+F4" },
        }, keys), L"&Datei");
        AddPopup(bar, Popup({
            { CmdUndo, L"&Rückgängig", L"Strg+Z", Ctrl, 'Z' },
            { CmdRedo, L"&Wiederholen", L"Strg+Y", Ctrl, 'Y' },
            {},
            { CmdCut, L"&Ausschneiden", L"Strg+X", Ctrl, 'X' },
            { CmdCopy, L"&Kopieren", L"Strg+C", Ctrl, 'C' },
            { CmdPaste, L"&Einfügen", L"Strg+V", Ctrl, 'V' },
            {},
            { CmdDuplicate, L"&Duplizieren", L"Strg+D", Ctrl, 'D' },
            { CmdDelete, L"&Löschen", L"Entf", 0, VK_DELETE },
            {},
            { CmdSelectAll, L"Alles a&uswählen", L"Strg+A", Ctrl, 'A' },
            { CmdClearSelection, L"Auswahl auf&heben", L"Esc", 0, VK_ESCAPE },
            {},
            { CmdFind, L"&Suchen…", L"Strg+F", Ctrl, 'F' },
            { CmdFindNext, L"&Weitersuchen", L"F3", 0, VK_F3 },
            { CmdFindPrevious, L"&Rückwärts suchen", L"Umschalt+F3", FSHIFT, VK_F3 },
        }, keys), L"&Bearbeiten");
        AddPopup(bar, Popup({
            { CmdInsertPage, L"Seite &einfügen" },
            { CmdInsertFromFile, L"Seiten aus &PDF oder Dokument einfügen…" },
            {},
            { CmdMovePageUp, L"Seite nach &oben" },
            { CmdMovePageDown, L"Seite nach &unten" },
            {},
            { CmdDeletePage, L"Seite &löschen" },
        }, keys), L"&Seite");
        // Zoom and page shortcuts as Acrobat and SumatraPDF have them (Strg+0 Ganze Seite, Strg+1 Originalgröße, Strg+2
        // Seitenbreite); Zurück/Vorwärts as browsers and Edge's PDF viewer (Alt+Links/Rechts, the mouse's side buttons).
        constexpr BYTE Alt = FALT;
        AddPopup(bar, Popup({
            { CmdTogglePane, L"&Dateien und Aufgaben", L"Strg+B", Ctrl, 'B' },
            {},
            { CmdZoomIn, L"&Vergrößern", L"Strg+Plus", Ctrl, VK_OEM_PLUS },
            { CmdZoomOut, L"Ver&kleinern", L"Strg+Minus", Ctrl, VK_OEM_MINUS },
            { CmdActualSize, L"&Originalgröße (100 %)", L"Strg+1", Ctrl, '1' },
            { CmdFitPage, L"&Ganze Seite", L"Strg+0", Ctrl, '0' },
            { CmdFitWidth, L"&Seitenbreite", L"Strg+2", Ctrl, '2' },
            {},
            { CmdGoToPage, L"Gehe &zu Seite…", L"Strg+G", Ctrl, 'G' },
            { CmdBack, L"Zu&rück", L"Alt+Links", Alt, VK_LEFT },
            { CmdForward, L"Vor&wärts", L"Alt+Rechts", Alt, VK_RIGHT },
        }, keys), L"&Ansicht");
        keys.push_back({ FVIRTKEY | FCONTROL, VK_ADD, CmdZoomIn });       // the number pad's + and - too
        keys.push_back({ FVIRTKEY | FCONTROL, VK_SUBTRACT, CmdZoomOut });
        keys.push_back({ FVIRTKEY | FCONTROL, VK_NUMPAD0, CmdFitPage });
        keys.push_back({ FVIRTKEY | FCONTROL, VK_NUMPAD1, CmdActualSize });
        keys.push_back({ FVIRTKEY | FCONTROL, VK_NUMPAD2, CmdFitWidth });
        keys.push_back({ FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS, CmdZoomIn }); // some layouts need Umschalt for +

        // Datei -> Zuletzt geöffnet: a submenu in place of its placeholder, filled by SetRecentFiles.
        recent = CreatePopupMenu();
        if (!recent)
        {
            ThrowLastError("CreatePopupMenu(recent files)");
        }
        HMENU file = GetSubMenu(bar, 0);
        MENUITEMINFOW info{ sizeof(info) };
        info.fMask = MIIM_SUBMENU | MIIM_ID;
        info.wID = 0;
        info.hSubMenu = recent;
        if (!SetMenuItemInfoW(file, CmdRecentFirst, FALSE, &info))
        {
            ThrowLastError("SetMenuItemInfo(recent files)");
        }
        SetRecentFiles({});

        accelerators = CreateAcceleratorTableW(keys.data(), static_cast<int>(keys.size()));
        if (!accelerators)
        {
            ThrowLastError("CreateAcceleratorTable");
        }
    }

    MainMenu::~MainMenu()
    {
        if (accelerators)
        {
            DestroyAcceleratorTable(accelerators);
        }
        // The bar belongs to the window once set (destroyed with it).
    }

    void MainMenu::SetRecentFiles(std::vector<std::filesystem::path> const& files)
    {
        while (GetMenuItemCount(recent) > 0)
        {
            if (!DeleteMenu(recent, 0, MF_BYPOSITION)) ThrowLastError("DeleteMenu(recent files)");
        }
        if (files.empty())
        {
            if (!AppendMenuW(recent, MF_STRING | MF_GRAYED, 0, L"Keine zuletzt geöffneten Dateien"))
            {
                ThrowLastError("AppendMenu(recent files)");
            }
            return;
        }
        for (size_t i = 0; i < files.size() && CmdRecentFirst + static_cast<int>(i) <= CmdRecentLast; ++i)
        {
            // "&1 C:\...\Blatt.xopp": the number as the access key, the path shortened in the middle like Windows does,
            // & doubled; a file that is gone is shown disabled (honest: it cannot be opened).
            wchar_t compact[MAX_PATH]{};
            if (!PathCompactPathExW(compact, files[i].c_str(), 64, 0)) wcsncpy_s(compact, files[i].c_str(), _TRUNCATE);
            std::wstring path;
            for (wchar_t const* c = compact; *c; ++c)
            {
                path += *c;
                if (*c == L'&') path += L'&';
            }
            std::wstring text = i < 9 ? std::format(L"&{} {}", i + 1, path) : std::format(L"1&0 {}", path);
            bool exists = GetFileAttributesW(files[i].c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!AppendMenuW(recent, MF_STRING | (exists ? MF_ENABLED : MF_GRAYED), static_cast<UINT_PTR>(CmdRecentFirst + i), text.c_str()))
            {
                ThrowLastError("AppendMenu(recent files)");
            }
        }
    }

    void MainMenu::Enable(int command, bool enabled)
    {
        EnableMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    }

    void MainMenu::Check(int command, bool checked)
    {
        CheckMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    }
}
