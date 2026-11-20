#include "MainMenu.h"

#include "Commands.h"
#include "Resources.h"
#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <shlwapi.h>

#include <format>
#include <string>

namespace xournal::app
{
    MainMenu::MainMenu()
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDA_MAIN));
        if (!accelerators) ThrowLastError("LoadAccelerators (main menu)");

        using MenuHandle = std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(&DestroyMenu)>;
        MenuHandle mainMenu(LoadMenuW(instance, MAKEINTRESOURCEW(IDM_MAIN)), DestroyMenu);
        if (!mainMenu) ThrowLastError("LoadMenu (main menu)");
        MenuHandle recentMenu(CreatePopupMenu(), DestroyMenu);
        if (!recentMenu) ThrowLastError("CreatePopupMenu (recent files)");

        HMENU file = GetSubMenu(mainMenu.get(), 0);
        MENUITEMINFOW info{ sizeof(info) };
        info.fMask = MIIM_SUBMENU | MIIM_ID;
        info.hSubMenu = recentMenu.get();
        if (!SetMenuItemInfoW(file, CmdRecentFirst, FALSE, &info))
            ThrowLastError("SetMenuItemInfo (recent files)");
        recent = recentMenu.release();
        SetRecentFiles({});
        bar = mainMenu.release();
    }

    MainMenu::~MainMenu()
    {
        if (ownsBar && IsMenu(bar) && !DestroyMenu(bar))
            LogError("DestroyMenu (main menu) failed");
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
