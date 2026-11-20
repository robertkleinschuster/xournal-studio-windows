#pragma once

// A viewer's way through the document: jumps (a link, an outline entry, a search hit picked from the list, a page in the
// pages pane, Gehe zu Seite) remember where the view was, so Zurück (Alt+Links, the mouse's back button) returns there
// and Vorwärts goes to the jump's target again (InkPages' ViewHistory: places follow page operations). Also the zoom
// commands with a PDF viewer's meaning of 100 % (the page's real size) and the zoom in percent for the status bar.

#include "inkpages/WritingSurface.h"
#include "inkpages/model/ViewHistory.h"

#include <windows.h>

#include <functional>

namespace xournal::app
{
    class ViewNavigation
    {
    public:
        ViewNavigation(HWND owner, inkpages::WritingSurface& surface) : owner(owner), surface(surface) {}

        /// Remembers where the view is (before a jump: Zurück comes here).
        void Remember();
        /// Remembers where the view is, then runs `go` (the jump).
        void Jump(std::function<void()> const& go);
        void Back();
        void Forward();
        bool CanGoBack() const { return history.CanGoBack(); }
        bool CanGoForward() const { return history.CanGoForward(); }
        /// Asks for a page (Gehe zu Seite) and jumps there.
        void GoToPage();
        /// A new document: no way back into the old one.
        void DocumentShown() { history.Clear(); }
        void PagesRemapped(inkpages::PageMap const& map) { history.Remap(map); }

        void ZoomIn();
        void ZoomOut();
        void ActualSize(); // 100 %: a page unit (a point) shown at its real size
        /// The zoom as PDF viewers show it: 100 % = the page's real size (a point is 1/72 inch, a DIP 1/96 inch).
        int ZoomPercent() const;

    private:
        HWND owner;
        inkpages::WritingSurface& surface;
        inkpages::ViewHistory history;
    };
}
