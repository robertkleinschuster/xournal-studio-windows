#pragma once

// The side pane's "Seiten" tab (the C# PagesPane): the document's pages as thumbnails, one below the other, in a
// comctl32 ListView (icon view; one item per page; the thumbnails are drawn in custom
// draw from PageThumbnails). A tap goes to the page; the selected page follows the page in view. A comctl32 toolbar
// above the list moves the selected page up or down ("Nach oben" / "Nach unten", disabled at the first / last page);
// the context menu inserts a blank page before/after, pages of a PDF or document, moves up/down, deletes (asked by the
// window). The pane only asks: the window carries the actions out.

#include "PageThumbnails.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>

namespace xournal::app
{
    class PagesPane
    {
    public:
        PagesPane(HWND parent, PageThumbnails& thumbnails);
        ~PagesPane();
        PagesPane(PagesPane const&) = delete;
        PagesPane& operator=(PagesPane const&) = delete;

        HWND Window() const { return window; }
        void SetDarkMode(bool dark);

        /// The document has `count` pages now (the list is drawn anew).
        void SetCount(int count);
        /// The page in view: selected (no navigation).
        void Select(int index);
        /// A page's thumbnail changed.
        void Redraw(int index);

        std::function<void(int index)> goTo;
        std::function<void(int index)> insertBlank;    // a blank page at this index
        std::function<void(int index)> insertFromFile; // pages of a PDF or document around this page
        std::function<void(int from, int to)> move;
        std::function<void(int index)> remove;         // the window asks first

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        static LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        LRESULT OnNotify(NMHDR const& header);
        LRESULT OnCustomDraw(NMLVCUSTOMDRAW& draw);
        void UpdateThumbnailFocus();
        void DrawThumbnail(HDC dc, RECT const& area, int index);
        void ShowMenu(int index, POINT screen);
        void CreateBar();
        void Layout();
        void PositionItem(int index);
        void UpdateSizes();
        void UpdateBarImages();
        void ReleaseImageLists();
        SIZE Box() const; // the thumbnails' box in an item's image slot
        void UpdateButtons();
        void MoveBy(int index, int delta);
        int Selected() const;

        HWND window = nullptr;
        HWND bar = nullptr;              // Nach oben / Nach unten
        HWND list = nullptr;
        HIMAGELIST images = nullptr;     // one blank image: gives the items their size (the thumbnails are drawn over it)
        HIMAGELIST barImages = nullptr;  // the toolbar's glyphs
        PageThumbnails& thumbnails;
        int count = 0;
        bool selecting = false;          // a selection set in code: no navigation
        bool darkMode = false;
        int thumbWidth = 0, thumbHeight = 0;
        int itemX = 0, itemPitch = 0, wheelRemainder = 0;
    };
}
