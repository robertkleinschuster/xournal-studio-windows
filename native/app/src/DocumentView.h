#pragma once

// Shows a document on the writing surface: its pages (sizes from the model), their ink through PageInkBridge, what lies
// under the ink through the page underlay (backgrounds, PDF pages rendered on the render thread, pictures, texts, drawn
// strokes), the file's pictures in the selection through FilePictures. Before saving, CaptureInk brings the live pages' strokes
// into the document and runs the snapshot under the document's lock.
//
// Lifetime: the view must go before the surface (the window releases it first). Its destructor takes its UI-thread
// callbacks and the picture host back from the surface; the ink-thread callbacks (load/store) hold the shared bridge set,
// never the view, so one that still runs while the window closes finds the documents, not freed memory.

#include "FilePictures.h"
#include "PageInkBridges.h"
#include "PageUnderlay.h"
#include "RenderQueue.h"

#include "inkpages/WritingSurface.h"

#include <functional>
#include <memory>
#include <mutex>

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

        /// The live pages' current strokes go into the document, then `work` runs with the document's lock held (UI
        /// thread; take the snapshot there).
        void CaptureInk(std::function<void()> const& work);

    private:
        void DrawUnderlay(int page, ID2D1DeviceContext* dc, inkpages::RectF area);
        void OnViewChanged();

        inkpages::WritingSurface& surface;
        std::shared_ptr<PageInkBridges> bridges = std::make_shared<PageInkBridges>(); // shared with load/store
        RenderQueue renderQueue;                             // one render thread and PDF device for the app
        std::uint64_t generation = 0;                        // the shown document's number (render keys)
        std::unique_ptr<FilePictures> pictures;              // the shown document's (UI thread)
        std::unique_ptr<PageUnderlay> underlay;              // the shown document's (UI thread)
    };
}
