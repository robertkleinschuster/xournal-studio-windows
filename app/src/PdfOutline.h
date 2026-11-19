#pragma once

// The background PDF's outline in the side pane: shown in the Gliederung tab when the document's PDF has one (read
// once its navigation is loaded), the tab hidden otherwise; a chosen entry goes to its place (PdfLinks).

#include "DocumentView.h"
#include "OutlinePane.h"
#include "PdfLinks.h"
#include "SidePane.h"

#include <memory>

namespace xournal::app
{
    class PdfOutline
    {
    public:
        PdfOutline(SidePane& pane, DocumentView& view, PdfLinks& links);

        /// A document was shown or its PDF (re)loaded: the outline follows (UI thread).
        void Update();

    private:
        SidePane& pane;
        DocumentView& view;
        std::unique_ptr<OutlinePane> outline;
        std::shared_ptr<pdf::PdfNavigation const> shownFor; // the navigation the tree shows
    };
}
