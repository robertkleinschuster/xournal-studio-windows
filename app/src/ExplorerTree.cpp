#include "ExplorerTree.h"

#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/project/ProjectFiles.h"

#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <cstring>
#include <format>

namespace xournal::app
{
    namespace
    {
        // Git colors of the explorer (as code editors' git views): new green, modified blue - darker on a light window
        // background, lighter on a dark one, so they stay readable either way.
        COLORREF GitColor(git::FileState state)
        {
            COLORREF background = GetSysColor(COLOR_WINDOW);
            bool dark = (GetRValue(background) * 299 + GetGValue(background) * 587 + GetBValue(background) * 114) / 1000 < 128;
            if (state == git::FileState::New)
            {
                return dark ? RGB(0x6C, 0xCB, 0x5F) : RGB(0x10, 0x7C, 0x10);
            }
            if (state == git::FileState::Deleted)
            {
                return dark ? RGB(0xFF, 0x99, 0xA4) : RGB(0xC4, 0x2B, 0x1C);
            }
            return dark ? RGB(0x60, 0xCD, 0xFF) : RGB(0x00, 0x5F, 0xB8);
        }

        // The shell's small icon for a file or folder in the system image list, as File Explorer shows it. By name and
        // attributes (SHGFI_USEFILEATTRIBUTES): the type's icon without touching the file; `ofFile`: the file's own icon
        // (read from the file, for the types whose files each have their own).
        int ShellIcon(std::wstring const& name, bool folder, bool open = false, bool ofFile = false)
        {
            SHFILEINFOW info{};
            UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | (ofFile ? 0 : SHGFI_USEFILEATTRIBUTES) | (open ? SHGFI_OPENICON : 0);
            if (!SHGetFileInfoW(name.c_str(), folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), flags))
            {
                LogError(std::format("explorer: no shell icon for {}", ToUtf8(name)));
                return 0;
            }
            return info.iIcon;
        }

        enum MenuId : UINT
        {
            MenuOpen = 1,
            MenuNewDocument,
            MenuNewFolder,
            MenuRename,
            MenuDelete,
            MenuDiscard,
            MenuShowInExplorer,
            MenuCopyPath,
            MenuCut,
            MenuCopy,
            MenuPaste,
        };
    }

    ExplorerTree::ExplorerTree(HWND parent, int id)
    {
        tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
            TVS_FULLROWSELECT | TVS_TRACKSELECT | TVS_EDITLABELS, 0, 0, 0, 0, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!tree)
        {
            ThrowLastError("CreateWindowEx(TreeView)");
        }
        // TVS_LINESATROOT: without it the top-level items get no expand button (documented for TVS_HASBUTTONS); it only
        // takes effect with TVS_HASLINES (comctl32 dropped it otherwise - read back 2026-11-19). The Explorer theme draws
        // File Explorer's chevrons and no lines.
        // File Explorer's look for the tree (hover, expand arrows).
        HRESULT hr = SetWindowTheme(tree, L"Explorer", nullptr);
        if (FAILED(hr))
        {
            LogError(std::format("explorer: theme not set: {}", DescribeHr(hr)));
        }
        TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
        // The system image list (shared, owned by the shell: the tree must not destroy it - no TVS_SHAREIMAGELISTS
        // needed for a TreeView, which never destroys its image lists).
        SHFILEINFOW info{};
        auto images = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(L"C:\\", 0, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
        if (!images)
        {
            LogError("explorer: system image list not available (shown without icons)");
        }
        else
        {
            TreeView_SetImageList(tree, images, TVSIL_NORMAL);
        }
    }

    ExplorerTree::~ExplorerTree()
    {
        if (struckFont) DeleteObject(struckFont);
    }

    bool ExplorerTree::HasFocus() const
    {
        HWND focus = GetFocus();
        return focus == tree || (focus && GetParent(focus) == tree); // the tree or its label edit
    }

    void ExplorerTree::Show(ProjectSnapshot next)
    {
        if (TreeView_GetEditControl(tree))
        {
            // A name is being edited: the tree is rebuilt when the edit ends (rebuilding would end it).
            deferred = std::move(next);
            return;
        }
        if (!pendingSelection)
        {
            if (Node const* selected = NodeOf(TreeView_GetSelection(tree)))
            {
                pendingSelection = selected->path;
            }
        }
        snapshot = std::move(next);
        SendMessageW(tree, WM_SETREDRAW, FALSE, 0);
        TreeView_DeleteAllItems(tree);
        nodes.clear();
        for (auto const& entry : snapshot.root.children)
        {
            Add(TVI_ROOT, entry);
        }
        if (pendingSelection)
        {
            if (HTREEITEM item = Find(*pendingSelection))
            {
                TreeView_SelectItem(tree, item);
                TreeView_EnsureVisible(tree, item);
            }
            pendingSelection.reset();
        }
        SendMessageW(tree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(tree, nullptr, TRUE);
    }

    void ExplorerTree::Add(HTREEITEM parent, project::ProjectEntry const& entry)
    {
        nodes.push_back({ entry.path, entry.isFolder, entry.missing });
        std::wstring name = entry.Name();
        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE |
            (entry.isFolder ? TVIF_EXPANDEDIMAGE : 0);
        insert.itemex.pszText = name.data();
        insert.itemex.lParam = static_cast<LPARAM>(nodes.size() - 1);
        insert.itemex.cChildren = entry.children.empty() ? 0 : 1;
        insert.itemex.iImage = insert.itemex.iSelectedImage = HasOwnIcon(entry) && !entry.missing ? ShellIcon(entry.path.native(), false, false, true)
            : Icon(name, entry.isFolder, false);
        insert.itemex.iExpandedImage = entry.isFolder ? Icon(name, true, true) : 0;
        HTREEITEM item = TreeView_InsertItem(tree, &insert);
        if (!item)
        {
            ThrowLastError("TreeView_InsertItem");
        }
        for (auto const& child : entry.children)
        {
            Add(item, child);
        }
        if (entry.isFolder && expanded.contains(entry.path.native()))
        {
            TreeView_Expand(tree, item, TVE_EXPAND);
        }
    }

    // Programs, shortcuts and icon files show their own icon in File Explorer (their type has no single one); every
    // other file shows its type's icon - read once per type, not per file.
    bool ExplorerTree::HasOwnIcon(project::ProjectEntry const& entry)
    {
        if (entry.isFolder)
        {
            return false;
        }
        std::wstring extension = entry.path.extension().native();
        for (wchar_t const* own : { L".exe", L".lnk", L".ico", L".url" })
        {
            if (EqualsIgnoreCase(extension, own)) return true;
        }
        return false;
    }

    int ExplorerTree::Icon(std::wstring const& name, bool folder, bool open)
    {
        // Per file type (extension) once: the same icon for every file of a type.
        std::wstring key = folder ? (open ? L"<open folder>" : L"<folder>") : std::filesystem::path(name).extension().native();
        auto it = icons.find(key);
        if (it == icons.end())
        {
            it = icons.emplace(key, ShellIcon(folder ? L"folder" : L"file" + key, folder, open)).first;
        }
        return it->second;
    }

    ExplorerTree::Node const* ExplorerTree::NodeOf(HTREEITEM item) const
    {
        if (!item)
        {
            return nullptr;
        }
        TVITEMW info{ TVIF_PARAM };
        info.hItem = item;
        if (!TreeView_GetItem(tree, &info) || info.lParam < 0 || static_cast<size_t>(info.lParam) >= nodes.size())
        {
            return nullptr;
        }
        return &nodes[static_cast<size_t>(info.lParam)];
    }

    HTREEITEM ExplorerTree::Find(std::filesystem::path const& p) const
    {
        // Depth first over the items (the tree holds every item; folders are only opened on the way to the item).
        std::vector<HTREEITEM> pending;
        for (HTREEITEM item = TreeView_GetRoot(tree); item; item = TreeView_GetNextSibling(tree, item))
        {
            pending.push_back(item);
        }
        while (!pending.empty())
        {
            HTREEITEM item = pending.back();
            pending.pop_back();
            if (Node const* node = NodeOf(item); node && project::ProjectFiles::SamePath(node->path, p))
            {
                return item;
            }
            for (HTREEITEM child = TreeView_GetChild(tree, item); child; child = TreeView_GetNextSibling(tree, child))
            {
                pending.push_back(child);
            }
        }
        return nullptr;
    }

    void ExplorerTree::Select(std::filesystem::path const& p)
    {
        if (HTREEITEM item = Find(p))
        {
            TreeView_SelectItem(tree, item);
            TreeView_EnsureVisible(tree, item);
            return;
        }
        pendingSelection = p;
    }

    void ExplorerTree::BeginRename(std::filesystem::path const& p)
    {
        if (HTREEITEM item = Find(p))
        {
            SetFocus(tree);
            TreeView_SelectItem(tree, item);
            TreeView_EnsureVisible(tree, item);
            TreeView_EditLabel(tree, item);
        }
    }

    void ExplorerTree::Request(ExplorerCommand command, Node const& node, std::wstring newName)
    {
        if (requested)
        {
            requested({ command, node.path, node.isFolder, std::move(newName) });
        }
    }

    std::optional<std::filesystem::path> ExplorerTree::SelectedFolder() const
    {
        Node const* node = NodeOf(TreeView_GetSelection(tree));
        if (!node || node->missing) return std::nullopt;
        return node->isFolder ? node->path : node->path.parent_path();
    }

    void ExplorerTree::RequestForSelection(ExplorerCommand command)
    {
        HTREEITEM item = TreeView_GetSelection(tree);
        if (Node const* node = NodeOf(item))
        {
            if (command == ExplorerCommand::Rename)
            {
                if (!node->missing) TreeView_EditLabel(tree, item);
                return;
            }
            bool needsFile = command != ExplorerCommand::Discard && command != ExplorerCommand::CopyPath;
            if (node->missing && needsFile)
            {
                return; // a deleted item can only be restored (its context menu offers nothing else either)
            }
            Request(command, *node);
        }
        else if (command == ExplorerCommand::Paste && snapshot.folder)
        {
            Request(command, { *snapshot.folder, true });
        }
    }

    void ExplorerTree::ShowMenu(Node const& node, bool onItem, POINT screen)
    {
        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            ThrowLastError("CreatePopupMenu");
        }
        auto add = [&](UINT id, wchar_t const* text, bool enabled = true)
        {
            if (!AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id, text))
            {
                ThrowLastError("AppendMenu");
            }
        };
        auto separator = [&] { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); };
        bool onFile = onItem && !node.missing; // a deleted item is not on disk: it can only be restored
        if (onItem && !node.isFolder)
        {
            add(MenuOpen, L"Ö&ffnen", onFile);
            separator();
        }
        add(MenuNewDocument, L"Neues &Dokument", !node.missing);
        add(MenuNewFolder, L"Neuer O&rdner", !node.missing);
        separator();
        if (onItem)
        {
            add(MenuCut, L"&Ausschneiden\tStrg+X", onFile);
            add(MenuCopy, L"&Kopieren\tStrg+C", onFile);
        }
        add(MenuPaste, L"&Einfügen\tStrg+V", !node.missing && IsClipboardFormatAvailable(CF_HDROP));
        if (onItem)
        {
            separator();
            add(MenuRename, L"&Umbenennen\tF2", onFile);
            add(MenuDelete, L"&Löschen\tEntf", onFile);
            add(MenuDiscard, L"Änderungen &verwerfen", snapshot.StateOf(node.path) != git::FileState::Unchanged);
            separator();
            add(MenuShowInExplorer, L"Im &Explorer anzeigen", onFile);
            add(MenuCopyPath, L"Als &Pfad kopieren\tStrg+Umschalt+C");
        }
        UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y,
            GetParent(tree), nullptr));
        DestroyMenu(menu);
        Node target = node; // the menu ran a message loop: the nodes may have been replaced meanwhile
        switch (chosen)
        {
        case MenuOpen: Request(ExplorerCommand::Open, target); break;
        case MenuNewDocument: Request(ExplorerCommand::NewDocument, target); break;
        case MenuNewFolder: Request(ExplorerCommand::NewFolder, target); break;
        case MenuRename: BeginRename(target.path); break;
        case MenuDelete: Request(ExplorerCommand::Delete, target); break;
        case MenuDiscard: Request(ExplorerCommand::Discard, target); break;
        case MenuShowInExplorer: Request(ExplorerCommand::ShowInExplorer, target); break;
        case MenuCopyPath: Request(ExplorerCommand::CopyPath, target); break;
        case MenuCut: Request(ExplorerCommand::Cut, target); break;
        case MenuCopy: Request(ExplorerCommand::Copy, target); break;
        case MenuPaste: Request(ExplorerCommand::Paste, target); break;
        default: break; // closed without a choice
        }
    }

    void ExplorerTree::OnContextMenu(LPARAM lp)
    {
        if (!snapshot.folder)
        {
            return;
        }
        POINT screen{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        HTREEITEM item = nullptr;
        if (screen.x == -1 && screen.y == -1)
        {
            // From the keyboard (Shift+F10, the menu key): at the selected item.
            item = TreeView_GetSelection(tree);
            RECT rc{};
            if (item && TreeView_GetItemRect(tree, item, &rc, TRUE))
            {
                screen = { rc.left, rc.bottom };
            }
            ClientToScreen(tree, &screen);
        }
        else
        {
            TVHITTESTINFO hit{};
            hit.pt = screen;
            ScreenToClient(tree, &hit.pt);
            // The whole row is the item (full-row selection): right of the label or on its indent counts as well, not
            // only the label and icon (TVHT_ONITEM) - otherwise a click there meant the project folder.
            item = TreeView_HitTest(tree, &hit);
            if (item && (hit.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT | TVHT_ONITEMBUTTON)))
            {
                TreeView_SelectItem(tree, item);
            }
            else
            {
                item = nullptr;
            }
        }
        if (Node const* node = NodeOf(item))
        {
            ShowMenu(*node, true, screen);
        }
        else
        {
            ShowMenu({ *snapshot.folder, true }, false, screen); // the empty space: the project folder
        }
    }

    // The tree's font struck through, made again whenever the tree's font changed (a new font after a DPI change or a
    // setting) - checked once per paint.
    void ExplorerTree::UpdateStruckFont()
    {
        LOGFONTW font{};
        auto current = reinterpret_cast<HFONT>(SendMessageW(tree, WM_GETFONT, 0, 0));
        if (!GetObjectW(current ? current : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT)), sizeof(font), &font))
        {
            LogError("explorer: the tree's font is not readable (deleted items shown without strike-through)");
            return;
        }
        font.lfStrikeOut = TRUE;
        if (struckFont && std::memcmp(&font, &struckFrom, sizeof(font)) == 0)
        {
            return;
        }
        HFONT made = CreateFontIndirectW(&font);
        if (!made)
        {
            LogError("explorer: CreateFontIndirect failed (deleted items shown without strike-through)");
            return;
        }
        if (struckFont) DeleteObject(struckFont);
        struckFont = made;
        struckFrom = font;
    }

    LRESULT ExplorerTree::CustomDraw(NMTVCUSTOMDRAW& draw)
    {
        switch (draw.nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            UpdateStruckFont();
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
        {
            auto index = static_cast<size_t>(draw.nmcd.lItemlParam);
            if (index < nodes.size())
            {
                git::FileState state = snapshot.StateOf(nodes[index].path);
                if (state != git::FileState::Unchanged)
                {
                    draw.clrText = GitColor(state);
                }
                if (nodes[index].missing)
                {
                    // Deleted: struck through in the deleted color (custom draw's documented font change).
                    draw.clrText = GitColor(git::FileState::Deleted);
                    if (!struckFont)
                    {
                        return CDRF_DODEFAULT; // not available (logged): the color alone
                    }
                    SelectObject(draw.nmcd.hdc, struckFont);
                    return CDRF_NEWFONT;
                }
            }
            return CDRF_DODEFAULT;
        }
        }
        return CDRF_DODEFAULT;
    }

    LRESULT ExplorerTree::OnNotify(NMHDR const& header)
    {
        switch (header.code)
        {
        case NM_CUSTOMDRAW:
            return CustomDraw(const_cast<NMTVCUSTOMDRAW&>(reinterpret_cast<NMTVCUSTOMDRAW const&>(header)));
        case TVN_ITEMEXPANDEDW:
        {
            auto const& info = reinterpret_cast<NMTREEVIEWW const&>(header);
            if (Node const* node = NodeOf(info.itemNew.hItem))
            {
                if (info.action & TVE_EXPAND)
                {
                    expanded.insert(node->path.native());
                }
                else if (info.action & TVE_COLLAPSE)
                {
                    expanded.erase(node->path.native());
                }
            }
            return 0;
        }
        case NM_DBLCLK:
        case NM_RETURN:
            if (Node const* node = NodeOf(TreeView_GetSelection(tree)); node && !node->isFolder && !node->missing)
            {
                Request(ExplorerCommand::Open, *node);
                return TRUE;
            }
            return FALSE;
        case TVN_KEYDOWN:
        {
            auto const& key = reinterpret_cast<NMTVKEYDOWN const&>(header);
            bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            if (key.wVKey == VK_F2)
            {
                RequestForSelection(ExplorerCommand::Rename);
                return TRUE;
            }
            if (key.wVKey == 'C' && ctrl && shift)
            {
                RequestForSelection(ExplorerCommand::CopyPath);
                return TRUE;
            }
            return FALSE;
        }
        case TVN_BEGINLABELEDITW:
        {
            auto const& info = reinterpret_cast<NMTVDISPINFOW const&>(header);
            Node const* node = NodeOf(info.item.hItem);
            if (!node || node->missing)
            {
                return TRUE; // no editing
            }
            // Only the name before the extension is edited, as File Explorer does.
            if (HWND edit = TreeView_GetEditControl(tree))
            {
                SetWindowTextW(edit, project::ProjectFiles::EditableName(node->path, node->isFolder).c_str());
                SendMessageW(edit, EM_SETSEL, 0, -1);
            }
            return FALSE;
        }
        case TVN_ENDLABELEDITW:
        {
            auto const& info = reinterpret_cast<NMTVDISPINFOW const&>(header);
            if (Node const* node = NodeOf(info.item.hItem); node && info.item.pszText)
            {
                // Carried out after the edit control is gone (the request rebuilds the tree).
                Request(ExplorerCommand::Rename, *node, info.item.pszText);
            }
            if (deferred)
            {
                // After the edit control is gone (the request below may refresh again; this shows the newest until then).
                UiQueue::Post("explorer: show deferred", [this]
                {
                    if (deferred && !TreeView_GetEditControl(tree))
                    {
                        auto next = std::move(*deferred);
                        deferred.reset();
                        Show(std::move(next));
                    }
                });
            }
            return FALSE; // the label changes with the refresh, not here
        }
        }
        return 0;
    }
}
