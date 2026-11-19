#pragma once

// The documents whose pages are (or were just) in the ink presenter, oldest first; the last is the shown document's.
// InkPages' ink thread calls load/store through this set, shared by the view and the surface's callbacks, so a callback
// that runs while the window closes never reaches a view that is gone.
//
// Switching documents: SetPages makes InkPages hand the old pages back (store) on its ink thread, in order before the
// new document's loads - so a store goes to the bridge that has that page live, a load to the newest bridge.

#include "PageInkBridge.h"

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace xournal::app
{
    class PageInkBridges
    {
    public:
        void Add(std::shared_ptr<PageInkBridge> bridge);
        std::shared_ptr<PageInkBridge> Current() const;

        /// Ink thread.
        std::vector<inkpages::KeyedStroke> Load(int page);
        /// Ink thread.
        void Store(int page, std::vector<inkpages::KeyedStroke> const& strokes);

    private:
        mutable std::mutex lock;
        std::deque<std::shared_ptr<PageInkBridge>> bridges;
    };
}
