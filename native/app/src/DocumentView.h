#pragma once

// Shows a document on the writing surface: its pages (sizes from the model), their ink through PageInkBridge, what lies
// under the ink through the page underlay (backgrounds, PDF pages rendered on the render thread, pictures, texts, drawn
// strokes), the file's pictures in the selection through FilePictures. Before saving, CaptureInk brings the live pages' strokes
// into the document and runs the snapshot under the document's lock.
//
// Switching documents: SetPages makes InkPages hand the old pages back (store) on its ink thread, in order before the
// new document's loads - so a store goes to the bridge that has that page live, a load to the newest bridge.

#include "FilePictures.h"
#include "PageInkBridge.h"
#include "PageUnderlay.h"
#include "RenderQueue.h"

#include "inkpages/WritingSurface.h"

#include <deque>
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
        std::shared_ptr<PageInkBridge> Current() const;
        std::vector<inkpages::KeyedStroke> Load(int page);
        void Store(int page, std::vector<inkpages::KeyedStroke> strokes);
        void DrawUnderlay(int page, ID2D1DeviceContext* dc, inkpages::RectF area);
        void OnViewChanged();

        inkpages::WritingSurface& surface;
        mutable std::mutex bridgesLock;
        std::deque<std::shared_ptr<PageInkBridge>> bridges; // oldest first; the last is the shown document's
        RenderQueue renderQueue;                             // one render thread and PDF device for the app
        std::uint64_t generation = 0;                        // the shown document's number (render keys)
        std::unique_ptr<FilePictures> pictures;              // the shown document's (UI thread)
        std::unique_ptr<PageUnderlay> underlay;              // the shown document's (UI thread)
    };
}
