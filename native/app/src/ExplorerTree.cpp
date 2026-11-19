#include "ExplorerTree.h"

#include "FolderListing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <format>

namespace xournal::app
{
    ExplorerTree::ExplorerTree(HWND parent, int id)
    {
        tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_SHOWSELALWAYS |
            TVS_FULLROWSELECT | TVS_TRACKSELECT, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (!tree)
        {
            ThrowLastError("CreateWindowEx(TreeView)");
        }
        // File Explorer's look for the tree (hover, expand arrows).
        HRESULT hr = SetWindowTheme(tree, L"Explorer", nullptr);
        if (FAILED(hr))
        {
            LogError(std::format("explorer: theme not set: {}", DescribeHr(hr)));
        }
        TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    }

    void ExplorerTree::SetFolder(std::optional<std::filesystem::path> folder)
    {
        TreeView_DeleteAllItems(tree);
        paths.clear();
        folders.clear();
        if (!folder)
        {
            return;
        }
        AddChildren(TVI_ROOT, *folder);
    }

    void ExplorerTree::AddChildren(HTREEITEM parent, std::filesystem::path const& folder)
    {
        for (auto const& entry : FolderListing::List(folder))
        {
            TVINSERTSTRUCTW insert{};
            insert.hParent = parent;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_CHILDREN;
            insert.item.pszText = const_cast<wchar_t*>(entry.name.c_str());
            insert.item.cChildren = entry.folder ? 1 : 0; // folders get their children when opened
            HTREEITEM item = TreeView_InsertItem(tree, &insert);
            if (!item)
            {
                ThrowLastError("TreeView_InsertItem");
            }
            paths[item] = entry.path;
            folders[item] = entry.folder;
        }
    }

    std::optional<std::filesystem::path> ExplorerTree::PathOf(HTREEITEM item) const
    {
        auto it = paths.find(item);
        return it == paths.end() ? std::nullopt : std::optional(it->second);
    }

    void ExplorerTree::ChooseSelected()
    {
        HTREEITEM item = TreeView_GetSelection(tree);
        auto path = PathOf(item);
        if (path && !folders[item] && fileChosen)
        {
            fileChosen(*path);
        }
    }

    LRESULT ExplorerTree::OnNotify(NMHDR const& header)
    {
        switch (header.code)
        {
        case TVN_ITEMEXPANDINGW:
        {
            auto const& info = reinterpret_cast<NMTREEVIEWW const&>(header);
            HTREEITEM item = info.itemNew.hItem;
            if (info.action == TVE_EXPAND && !TreeView_GetChild(tree, item))
            {
                if (auto path = PathOf(item))
                {
                    AddChildren(item, *path);
                }
            }
            return FALSE;
        }
        case NM_DBLCLK:
        case NM_RETURN:
            ChooseSelected();
            return TRUE;
        }
        return 0;
    }
}
