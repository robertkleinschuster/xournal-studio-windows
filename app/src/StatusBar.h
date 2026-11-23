#pragma once

#include "MessageKind.h"

// The status bar at the window's bottom (comctl32, no size grip), four parts from left to right: the project's git
// status ("Git: 3  ↑1  ↓2"), the latest message (takes the free width), "Seite n von m" and the zoom in percent, as PDF
// viewers show them. A double-click on the page part asks for "Gehe zu Seite".

#include <windows.h>

#include <functional>
#include <string>

namespace xournal::app
{
    class StatusBar
    {
    public:
        explicit StatusBar(HWND parent);

        HWND Window() const { return bar; }
        int Height() const;
        /// After the parent's WM_SIZE and DPI changes: the bar takes the width, the parts their widths.
        void Layout();
        /// `page` 0-based (-1: none), `percent` = the zoom as viewers count it (100 % = the page's real size).
        void Show(int page, int count, int percent);
        /// The git part: its text (empty: nothing shown) and the tooltip (one fact per line).
        void ShowGit(std::wstring const& text, std::wstring const& details);
        void ShowMessage(std::wstring const& text, MessageKind kind);
        /// WM_NOTIFY from the bar; true when handled.
        bool OnNotify(NMHDR const& header);

        std::function<void()> pageDoubleClicked;

    private:
        void SetPart(int part, std::wstring const& text, std::wstring& shown);
        void UpdatePart(int part, std::wstring const& text);

        HWND bar = nullptr;
        UINT partsDpi = 0;
        int partsWidth = -1;
        std::wstring gitText, gitTip, pageText, zoomText, messageText;
    };
}
