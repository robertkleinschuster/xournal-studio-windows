#pragma once

// Shows a document on the writing surface: its pages (sizes from the model), their ink through PageInkBridge, what lies
// under the ink through the page underlay (backgrounds, PDF pages rendered on the render thread, pictures, texts, drawn
// strokes), the file's pictures and texts in the selection through FilePictures, texts for the Text tool through
// FileTexts. Before saving, CaptureInk brings the live pages' strokes into the document and runs the snapshot under the
// document's lock.
//
// Lifetime: the view must go before the surface (the window releases it first). Its destructor takes its UI-thread
// callbacks and the picture host back from the surface; the ink-thread callbacks (load/store) hold the shared bridge set,
// never the view, so one that still runs while the window closes finds the documents, not freed memory.

#include "FilePictures.h"
#include "FileTexts.h"
#include "PageInkBridges.h"
#include "PageUnderlay.h"
#include "RenderQueue.h"

#include "inkpages/WritingSurface.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace xournal::app
{
    class DocumentView
    {
    public:
        explicit DocumentView(inkpages::WritingSurface& surface);
        ~DocumentView();
        DocumentView(DocumentView const&) = delete;
        DocumentView& operator=(DocumentView const&) = delete;

        void Show(std::shared_ptr<XoppDocument> doc);

        /// Inserts a page at `index` (InkPages keeps the history; the app's per-page data follows the same PageMap) and
        /// scrolls to it.
        void InsertPage(int index, std::shared_ptr<XoppPage> page);
        /// Inserts pages at `index` (pages of another PDF or document) and scrolls to the first.
        void InsertPages(int index, std::vector<std::shared_ptr<XoppPage>> pages);
        /// Moves a page (the pages pane; keeps the history as inserting does).
        void MovePage(int from, int to);
        /// Removes a page (cannot be undone: the caller asks first; the last page stays). False: nothing removed.
        bool RemovePage(int index);
        int PageCount() const;
        /// The page's size and paper, for a new page like it.
        std::shared_ptr<XoppPage> PageAt(int index) const;
        /// The page list changed (a page inserted, removed or moved): the document has changes the ink history does not
        /// know.
        std::function<void()> pagesChanged;
        /// A document is shown (thumbnails start over).
        std::function<void()> shown;
        /// An edit (or its undo/redo) changed these pages' content (InkPages' contentChanged).
        std::function<void(std::set<int> const&)> pagesEdited;
        /// The view moved or zoomed (the page in view may be another).
        std::function<void()> viewMoved;
        /// The background PDF and its navigation are loaded (the outline can be shown; UI thread).
        std::function<void()> pdfLoaded;

        /// The shown document (nothing before the first).
        std::shared_ptr<XoppDocument> Document() const;
        /// The shown document's background PDF on screen (nothing without one).
        std::shared_ptr<PdfBackground> Pdf() const;
        RenderQueue& Queue() { return renderQueue; }
        /// The document's PDF was written or newly chosen: the screen loads it anew and draws its pages again.
        void ReloadPdf();

        /// Marks a search hit (page units of `page`) on its page; page -1 takes the mark away.
        void Highlight(int page, std::vector<Rect> rects);

        /// The live pages' current strokes go into the document, then `work` runs with the document's lock held (UI
        /// thread; take the snapshot there). Throws when the ink cannot be read (InkPages' StrokesOf: timeout or ink-thread
        /// error) - then `work` does not run, and the save, copy or export it was for must fail visibly.
        void CaptureInk(std::function<void()> const& work);

    private:
        void MarkCurrentPage(); // after the page count changed
        void DrawUnderlay(int page, ID2D1DeviceContext* dc, inkpages::RectF area);
        void OnViewChanged();
        /// The document's page list and the UI-side data change with `map` (before InkPages is asked to rearrange).
        void Remap(inkpages::PageMap const& map, std::function<void(XoppDocument::PageList&)> const& change);

        inkpages::WritingSurface& surface;
        std::shared_ptr<PageInkBridges> bridges = std::make_shared<PageInkBridges>(); // shared with load/store
        RenderQueue renderQueue;                             // one render thread and PDF device for the app
        std::uint64_t generation = 0;                        // the shown document's number (render keys)
        std::unique_ptr<FilePictures> pictures;              // the shown document's (UI thread)
        std::unique_ptr<FileTexts> texts;                    // the shown document's, for the Text tool (UI thread)
        std::unique_ptr<PageUnderlay> underlay;              // the shown document's (UI thread)
    };
}
