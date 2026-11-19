#include "MainMenu.h"

#include "Commands.h"
#include "xournal/base/Error.h"

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
        }, keys), L"&Bearbeiten");
        AddPopup(bar, Popup({
            { CmdInsertPage, L"Seite &einfügen" },
            { CmdInsertFromFile, L"Seiten aus &PDF oder Dokument einfügen…" },
            {},
            { CmdDeletePage, L"Seite &löschen" },
        }, keys), L"&Seite");
        AddPopup(bar, Popup({
            { CmdTogglePane, L"&Dateien und Aufgaben", L"Strg+B", Ctrl, 'B' },
            {},
            { CmdFitWidth, L"&Seitenbreite", L"Strg+0", Ctrl, '0' },
            { CmdZoomIn, L"&Vergrößern", L"Strg+Plus", Ctrl, VK_OEM_PLUS },
            { CmdZoomOut, L"Ver&kleinern", L"Strg+Minus", Ctrl, VK_OEM_MINUS },
        }, keys), L"&Ansicht");
        keys.push_back({ FVIRTKEY | FCONTROL, VK_ADD, CmdZoomIn });       // the number pad's + and - too
        keys.push_back({ FVIRTKEY | FCONTROL, VK_SUBTRACT, CmdZoomOut });

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

    void MainMenu::Enable(int command, bool enabled)
    {
        EnableMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    }

    void MainMenu::Check(int command, bool checked)
    {
        CheckMenuItem(bar, static_cast<UINT>(command), MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    }
}
