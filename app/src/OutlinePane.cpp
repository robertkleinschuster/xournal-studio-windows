#include "OutlinePane.h"

#include "ChildControls.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <uxtheme.h>
#include <windowsx.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"XournalStudioOutlinePane";
        constexpr int TreeId = 1;
    }

    OutlinePane::OutlinePane(HWND parent)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        if (!GetClassInfoExW(instance, ClassName, &wc))
        {
            wc = { sizeof(wc) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = ClassName;
            if (!RegisterClassExW(&wc)) ThrowLastError("RegisterClassEx(outline pane)");
        }
        window = CreateWindowExW(0, ClassName, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr, instance, this);
        if (!window) ThrowLastError("CreateWindowEx(outline pane)");
        // TVS_LINESATROOT (only effective with TVS_HASLINES): the top-level entries get their expand chevron too - a single
        // tap on it opens an entry, handy with the finger (user, 2026-11-20). The Explorer theme draws chevrons, no lines.
        tree = CreateWindowExW(0, WC_TREEVIEWW, L"Inhalt der PDF", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS |
            TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_INFOTIP, 0, 0, 0, 0, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(TreeId)), instance, nullptr);
        if (!tree) ThrowLastError("CreateWindowEx(outline tree)");
        HRESULT hr = SetWindowTheme(tree, L"Explorer", nullptr); // File Explorer's tree look
        if (FAILED(hr)) LogError(std::format("outline pane: theme not set: {}", DescribeHr(hr)));
        TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL, TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL);
        ChildControls::UseMessageFont(tree);
    }

    OutlinePane::~OutlinePane()
    {
        if (window && IsWindow(window)) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }

    void OutlinePane::Show(std::vector<pdf::OutlineEntry> const& outline)
    {
        SendMessageW(tree, WM_SETREDRAW, FALSE, 0);
        TreeView_DeleteAllItems(tree);
        destinations.clear();
        Add(TVI_ROOT, outline);
        SendMessageW(tree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(tree, nullptr, TRUE);
    }

    void OutlinePane::Add(HTREEITEM parent, std::vector<pdf::OutlineEntry> const& entries)
    {
        for (auto const& entry : entries)
        {
            TVINSERTSTRUCTW insert{};
            insert.hParent = parent;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM;
            insert.item.pszText = const_cast<wchar_t*>(entry.title.c_str());
            insert.item.lParam = static_cast<LPARAM>(destinations.size());
            destinations.push_back(entry.destination);
            HTREEITEM item = TreeView_InsertItem(tree, &insert);
            if (!item) ThrowLastError("TreeView_InsertItem (outline)");
            Add(item, entry.children);
        }
    }

    void OutlinePane::Choose(HTREEITEM item)
    {
        if (!item) return;
        TVITEMW data{ TVIF_PARAM };
        data.hItem = item;
        if (!TreeView_GetItem(tree, &data)) return;
        auto index = static_cast<size_t>(data.lParam);
        if (index < destinations.size() && destinations[index])
        {
            if (chosen) chosen(*destinations[index]);
        }
        else
        {
            TreeView_Expand(tree, item, TVE_TOGGLE); // groups others only
        }
    }

    LRESULT OutlinePane::Handle(UINT message, WPARAM wp, LPARAM lp)
    {
        switch (message)
        {
        case WM_SIZE:
            MoveWindow(tree, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        case WM_SETFOCUS:
            SetFocus(tree);
            return 0;
        case WM_NOTIFY:
        {
            auto const& header = *reinterpret_cast<NMHDR const*>(lp);
            if (header.hwndFrom != tree) break;
            if (header.code == NM_CLICK)
            {
                // The item under the click (the selection changes only after this notification).
                TVHITTESTINFO hit{};
                DWORD at = GetMessagePos();
                hit.pt = { GET_X_LPARAM(at), GET_Y_LPARAM(at) };
                ScreenToClient(tree, &hit.pt);
                HTREEITEM item = TreeView_HitTest(tree, &hit);
                if (item && (hit.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT))) Choose(item); // not its button
                return 0;
            }
            if (header.code == TVN_KEYDOWN)
            {
                auto const& key = reinterpret_cast<NMTVKEYDOWN const&>(header);
                if (key.wVKey == VK_RETURN || key.wVKey == VK_SPACE)
                {
                    Choose(TreeView_GetSelection(tree));
                    return TRUE;
                }
                return 0;
            }
            break;
        }
        }
        return DefWindowProcW(window, message, wp, lp);
    }

    LRESULT CALLBACK OutlinePane::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        }
        auto self = reinterpret_cast<OutlinePane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->tree) return DefWindowProcW(hwnd, message, wp, lp);
        try
        {
            return self->Handle(message, wp, lp);
        }
        catch (...)
        {
            LogException(std::format("outline pane: message 0x{:X}", message));
            return 0;
        }
    }
}
