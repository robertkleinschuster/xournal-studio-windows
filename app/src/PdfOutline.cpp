#include "PdfOutline.h"

#include "xournal/base/Log.h"

#include <chrono>
#include <format>

namespace xournal::app
{
    PdfOutline::PdfOutline(SidePane& pane, DocumentView& view, PdfLinks& links)
        : pane(pane), view(view), outline(std::make_unique<OutlinePane>(pane.Window()))
    {
        pane.SetOutlineContent(outline->Window());
        outline->chosen = [&links](pdf::Destination const& destination) { links.Go(destination); };
    }

    void PdfOutline::Update()
    {
        auto background = view.Pdf();
        auto navigation = background ? background->Navigation() : nullptr;
        if (navigation == shownFor) return;
        shownFor = navigation;
        std::vector<pdf::OutlineEntry> entries;
        if (navigation)
        {
            try
            {
                auto start = std::chrono::steady_clock::now();
                entries = navigation->Outline();
                Log(std::format("outline: {} entries at the top, read in {} ms", entries.size(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
            }
            catch (...)
            {
                LogException("outline: reading the PDF's outline"); // the tab stays hidden
                entries.clear();
            }
        }
        outline->Show(entries);
        pane.ShowOutlineTab(!entries.empty());
    }
}
