#pragma once

// The status bar at the window's bottom (comctl32, no size grip): "Seite n von m" and the zoom in percent, as PDF viewers
// show them. A double-click on the page part asks for "Gehe zu Seite". Nothing interactive at the right edge.

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
        /// WM_NOTIFY from the bar; true when handled.
        bool OnNotify(NMHDR const& header);

        std::function<void()> pageDoubleClicked;

    private:
        void SetPart(int part, std::wstring const& text, std::wstring& shown);

        HWND bar = nullptr;
        std::wstring pageText, zoomText;
    };
}
