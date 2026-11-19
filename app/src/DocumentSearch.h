#pragma once

// Strg+F over the shown document (search/): the background PDF's text, the file's texts and the handwriting Windows
// recognises. A search runs on the thread pool from a snapshot of the document; its hits come page by page. The
// current hit is marked on its page (DocumentView::Highlight) and stepped through with Next/Previous. Handwriting is
// recognised ahead once a document is shown (search::RecognizeAll, stopped when another one is shown or a search
// starts) and kept in memory only, by the strokes it came from.
//
// Threads: the work never owns UI-thread objects: it gets the snapshot, the PDF's text document and the cache (shared,
// thread-safe), and posts its results back, where they count only while their search is the current one.

#include "DocumentView.h"

#include "xournal/search/Search.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace xournal::app
{
    class DocumentSearch
    {
    public:
        explicit DocumentSearch(DocumentView& view);
        ~DocumentSearch();
        DocumentSearch(DocumentSearch const&) = delete;
        DocumentSearch& operator=(DocumentSearch const&) = delete;

        /// Another document is shown: earlier hits and recognised handwriting go; recognition starts ahead.
        void DocumentShown();
        /// Pages were inserted, removed or moved (or the PDF changed): the hits' pages are no longer right - they go.
        void PagesChanged();

        /// Starts a search (an empty query only clears).
        void Start(std::wstring const& query);
        std::wstring const& Query() const { return query; }
        bool Running() const { return running; }

        std::vector<search::SearchHit> const& Hits() const { return hits; }
        std::optional<size_t> Current() const { return current; }
        /// Shows hit `index` (marked on its page; `shown` brings it into view).
        void Show(size_t index);
        void Next();
        void Previous();

        /// Hits arrived, the current hit changed or the search ended (UI thread): the pane shows it.
        std::function<void()> changed;
        /// A hit is to be shown: the window brings its place into view.
        std::function<void(search::SearchHit const&)> shown;

    private:
        struct Shared; // what the background work may hold
        void Cancel();
        void Clear();
        void RecognizeAhead();

        DocumentView& view;
        std::shared_ptr<Shared> shared;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
        std::stop_source stop;
        std::uint64_t generation = 0; // the current search's (results of older ones are dropped)
        std::wstring query;
        bool running = false;
        std::vector<search::SearchHit> hits;
        std::optional<size_t> current;
    };
}
