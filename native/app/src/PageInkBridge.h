#pragma once

// Lazy pages between InkPages and the document. A page coming near the view hands its strokes that can be ink to the
// presenter (load: PageInk::ReadStrokes, each with a key naming its <stroke> element); a page going far takes them back
// into its XML (store: Detach). Before a snapshot the live pages' current strokes (StrokesOf) are handed in (Refresh).
// InkPages carries the keys through copies, undo/redo and moves (KeyedStroke): a stroke with its key is written as its
// original element - moved or scaled by the key's transform, as the C# app's RecordTransform did (widths stay) - and
// key 0 is new ink, converted. A key may come back on another page (a selection moved across pages): that page writes a
// moved copy of the element, and the page it came from leaves it out.
// load/store run on InkPages' ink thread, everything else on the UI thread: the document is guarded by Lock().

#include "xournal/model/XoppDocument.h"

#include "inkpages/WritingSurface.h"
#include "inkpages/model/PageMap.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace xournal::app
{
    class PageInkBridge
    {
    public:
        /// Shares the document: a page released after another document is shown still has its strokes written back.
        explicit PageInkBridge(std::shared_ptr<XoppDocument> doc) : doc(std::move(doc)), inkPages(this->doc->Pages()) {}
        PageInkBridge(PageInkBridge const&) = delete;
        PageInkBridge& operator=(PageInkBridge const&) = delete;

        /// Ink thread: the page's strokes for the presenter (page coordinates).
        std::vector<inkpages::KeyedStroke> Load(int page);
        /// Ink thread: the page left the presenter with these strokes; they go into its XML.
        void Store(int page, std::vector<inkpages::KeyedStroke> const& strokes);

        /// The pages whose ink is in the presenter now (any thread).
        std::set<int> LivePages() const;
        bool IsLive(int page) const;
        XoppDocument& Document() const { return *doc; }

        /// UI thread, before a snapshot: a live page's current strokes (from StrokesOf). Call with Lock() held.
        void Refresh(int page, std::vector<inkpages::KeyedStroke> const& strokes);

        /// Ink thread (InkPages' pagesRemapped, in order with load/store): pages were inserted, removed or moved. The ink
        /// side's page list, live pages and keys follow `map`; a new page is taken from the document's list (the window
        /// changed it before it asked InkPages); a removed page's ink is dropped (InkPages drops it too).
        void Remapped(inkpages::PageMap const& map);

        /// Held while the document's pages are read or written (snapshot, load, store, underlay).
        std::mutex& Lock() const { return mutex; }

    private:
        // A page's strokes as the ink layer last gave them, for PageInk (ids: keys, or fresh ids for new ink).
        class HeldInk : public InkView
        {
        public:
            std::vector<StrokeId> ids;
            std::map<StrokeId, StrokeInk> inks; // new ink only (keyed strokes are written from their elements)
            std::vector<StrokeId> Strokes() const override { return ids; }
            Matrix TransformOf(StrokeId) const override { return {}; } // samples are where the ink shows them
            StrokeInk InkOf(StrokeId id) const override { return inks.at(id); }
        };

        // A stroke element handed out with a key: its element and its coordinates as loaded, and the page it lives on.
        struct Keyed
        {
            XmlElementPtr element;
            std::wstring loadedCoordinates;
            int page = 0;
        };

        void Hold(int page, HeldInk& held, std::vector<inkpages::KeyedStroke> const& strokes);

        std::shared_ptr<XoppDocument> doc;
        // The pages by InkPages' index as the ink thread sees them: load/store use it (the window changes the document's
        // own list at once, InkPages follows on its thread - pagesRemapped brings this list along in order).
        XoppDocument::PageList inkPages;
        mutable std::mutex mutex;
        std::map<int, std::unique_ptr<HeldInk>> live; // stable addresses: PageInk keeps a pointer to the view
        std::map<StrokeId, Keyed> keyed;              // every key handed out for this document
        StrokeId nextId = 1;
    };
}
