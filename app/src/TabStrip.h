#pragma once

// The tab strip above the document: one tab per open document, as in an IDE (comctl32 tab control, Windows' own look).
// It only shows what the session says and reports what the user asks: a tab chosen, closed (middle click, context
// menu). It holds no document logic.

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>
#include <vector>

namespace xournal::app
{
    struct TabEntry
    {
        std::wstring name;
        std::wstring tip; // the full path (empty: none)
        bool modified = false;
    };

    class TabStrip
    {
    public:
        explicit TabStrip(HWND parent);
        ~TabStrip();
        TabStrip(TabStrip const&) = delete;
        TabStrip& operator=(TabStrip const&) = delete;

        HWND Window() const { return strip; }
        /// The height the strip needs (one row; tabs that do not fit scroll with the control's own arrows).
        int Height() const;
        /// Shows `tabs` with `active` selected; only what differs is changed.
        void Set(std::vector<TabEntry> const& tabs, int active);
        /// WM_NOTIFY from the strip; true when handled (result in `result`).
        bool OnNotify(NMHDR const& header, LRESULT& result);

        std::function<void(int)> chosen;
        std::function<void(int)> closeRequested;
        std::function<void(int)> closeOthersRequested;
        /// The strip's font follows the monitor: its height changed.
        std::function<void()> dpiChanged;

    private:
        static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
        void ShowMenu(int tab);

        HWND strip = nullptr;
        std::vector<TabEntry> shown;
        std::wstring tipText;
    };
}
