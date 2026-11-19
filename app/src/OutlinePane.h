#pragma once

// The Gliederung tab: the background PDF's outline (bookmarks) in a comctl32 TreeView, as File Explorer's tree looks.
// A click on an entry, or Enter on the selected one, goes to its place (`chosen`); entries that only group others
// open and close. Replaced as a whole when another PDF is shown.

#include "xournal/pdf/PdfNavigation.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <optional>
#include <vector>

namespace xournal::app
{
    class OutlinePane
    {
    public:
        explicit OutlinePane(HWND parent);
        ~OutlinePane();
        OutlinePane(OutlinePane const&) = delete;
        OutlinePane& operator=(OutlinePane const&) = delete;

        HWND Window() const { return window; }

        /// Shows an outline (empty: nothing).
        void Show(std::vector<pdf::OutlineEntry> const& outline);

        /// An entry with a place was chosen.
        std::function<void(pdf::Destination const&)> chosen;

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void Add(HTREEITEM parent, std::vector<pdf::OutlineEntry> const& entries);
        void Choose(HTREEITEM item);

        HWND window = nullptr;
        HWND tree = nullptr;
        std::vector<std::optional<pdf::Destination>> destinations; // by an item's lParam
    };
}
