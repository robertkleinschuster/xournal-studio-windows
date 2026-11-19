#pragma once

// The side pane's "Seiten" tab (the C# PagesPane): the document's pages as thumbnails, one below the other, in a
// comctl32 ListView (icon view; one item per page with text and image by callback - not LVS_OWNERDATA, whose insert
// mark hit test finds nothing; the thumbnails are drawn in custom draw from PageThumbnails). A tap goes to the page; the selected page follows the page in view. The context menu inserts a blank
// page before/after, pages of a PDF or document, moves up/down, deletes (asked by the window); a drag moves a page
// (the ListView's insert mark shows where). The pane only asks: the window carries the actions out.

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
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        LRESULT OnNotify(NMHDR const& header);
        LRESULT OnCustomDraw(NMLVCUSTOMDRAW& draw);
        void DrawThumbnail(HDC dc, RECT const& area, int index);
        void ShowMenu(int index, POINT screen);
        void Layout();
        void UpdateSizes();
        int Selected() const;
        // Drag to move: the insert mark follows the pointer; the drop moves the page there.
        void DragMove(POINT client);
        void DragDrop();

        HWND window = nullptr;
        HWND list = nullptr;
        HIMAGELIST images = nullptr; // one blank image: gives the items their size (the thumbnails are drawn over it)
        PageThumbnails& thumbnails;
        int count = 0;
        bool selecting = false;      // a selection set in code: no navigation
        int dragFrom = -1;
        int thumbWidth = 0, thumbHeight = 0;
    };
}
