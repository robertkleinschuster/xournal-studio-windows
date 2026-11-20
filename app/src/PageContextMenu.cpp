#include "PageContextMenu.h"

#include "Commands.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <initializer_list>

namespace xournal::app::PageContextMenu
{
    namespace
    {
        struct Item
        {
            int command = 0; // 0: separator
            wchar_t const* text = L"";
            bool enabled = true;
        };

        // A menu destroyed when it goes out of scope (TrackPopupMenu returns after the choice).
        struct Menu
        {
            HMENU handle = CreatePopupMenu();
            ~Menu() { if (handle) DestroyMenu(handle); }
        };
    }

    int Show(HWND owner, POINT screen, POINT fallback, State const& state)
    {
        Menu menu;
        if (!menu.handle) ThrowLastError("CreatePopupMenu(page context menu)");
        // The texts and shortcuts as in the menu bar (MainMenu).
        for (Item const& item : std::initializer_list<Item>{
            { CmdCut, L"&Ausschneiden\tStrg+X", state.selection },
            { CmdCopy, L"&Kopieren\tStrg+C", state.selection || state.textSelection },
            { CmdPaste, L"&Einfügen\tStrg+V" },
            { CmdSelectAll, L"Alles a&uswählen\tStrg+A" },
            { CmdClearSelection, L"Auswahl auf&heben\tEsc", state.selection || state.textSelection },
            {},
            { CmdBack, L"&Zurück\tAlt+Links", state.canGoBack },
            { CmdForward, L"&Vorwärts\tAlt+Rechts", state.canGoForward },
            {},
            { CmdZoomIn, L"&Vergrößern\tStrg+Plus" },
            { CmdZoomOut, L"Ver&kleinern\tStrg+Minus" },
            { CmdActualSize, L"&Originalgröße\tStrg+1" },
            { CmdFitPage, L"&Ganze Seite\tStrg+0" },
            { CmdFitWidth, L"&Seitenbreite\tStrg+2" },
            {},
            { CmdGoToPage, L"&Gehe zu Seite…\tStrg+G" },
        })
        {
            BOOL added = item.command == 0 ? AppendMenuW(menu.handle, MF_SEPARATOR, 0, nullptr)
                : AppendMenuW(menu.handle, MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED),
                    static_cast<UINT_PTR>(item.command), item.text);
            if (!added) ThrowLastError("AppendMenu(page context menu)");
        }
        Log("page menu: shown");
        POINT at = screen.x == -1 && screen.y == -1 ? fallback : screen;
        return static_cast<int>(TrackPopupMenu(menu.handle, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, owner, nullptr));
    }

    bool ShowCopy(HWND owner, POINT screen)
    {
        Menu menu;
        if (!menu.handle) ThrowLastError("CreatePopupMenu (PDF text)");
        if (!AppendMenuW(menu.handle, MF_STRING, CmdCopy, L"&Kopieren"))
            ThrowLastError("AppendMenu (PDF text)");
        Log("page menu: touch PDF text copy");
        return TrackPopupMenu(menu.handle, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, owner, nullptr) == CmdCopy;
    }
}
