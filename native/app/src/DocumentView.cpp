#include "DocumentView.h"

#include "PageUnderlay.h"
#include "xournal/base/Log.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        // The page as InkPages lays it out: its size; plain paper in its color (the underlay draws the real background).
        inkpages::PageSpec SpecOf(XoppPage const& page)
        {
            inkpages::PageSpec spec;
            spec.size = { static_cast<float>(page.Width()), static_cast<float>(page.Height()) };
            spec.paper.kind = inkpages::PaperKind::Plain;
            Color c = page.IsPdf() || page.IsImageBackground() ? Colors::White : page.BackgroundColor();
            spec.paper.sheetColor = { c.a, c.r, c.g, c.b };
            return spec;
        }
    }

    DocumentView::DocumentView(inkpages::WritingSurface& surface) : surface(surface)
    {
        surface.load = [this](int page) { return Load(page); };
        surface.store = [this](int page, std::vector<inkpages::KeyedStroke> strokes) { Store(page, std::move(strokes)); };
        surface.drawUnderlay = [this](int page, ID2D1DeviceContext* dc, inkpages::RectF area) { DrawUnderlay(page, dc, area); };
        surface.viewChanged = [this] { OnViewChanged(); };
    }

    DocumentView::~DocumentView()
    {
        surface.SetPictureHost(nullptr);
        renderQueue.Stop(); // the PDF device goes while the app still runs
    }

    void DocumentView::OnViewChanged()
    {
        if (underlay)
        {
            underlay->Trim(surface.PageInView().value_or(0));
        }
    }

    std::shared_ptr<PageInkBridge> DocumentView::Current() const
    {
        std::scoped_lock guard(bridgesLock);
        return bridges.empty() ? nullptr : bridges.back();
    }

    void DocumentView::Show(std::shared_ptr<XoppDocument> doc)
    {
        std::vector<inkpages::PageSpec> specs;
        for (auto const& page : doc->Pages())
        {
            specs.push_back(SpecOf(*page));
        }
        auto bridge = std::make_shared<PageInkBridge>(doc);
        {
            std::scoped_lock guard(bridgesLock);
            bridges.push_back(bridge);
        }
        // The old document's renders that have not started are not needed any more.
        std::uint64_t old = generation++;
        renderQueue.Drop([old](std::uint64_t key) { return (key >> 32) == old; });
        auto invalidate = [this](int page) { surface.InvalidatePage(page); };
        surface.SetPictureHost(nullptr);
        pictures = std::make_unique<FilePictures>(doc, surface, bridge->Lock(), invalidate);
        underlay = std::make_unique<PageUnderlay>(renderQueue, *doc, generation, invalidate, *pictures);
        surface.SetPictureHost(pictures.get());
        surface.SetPages(std::move(specs));
        surface.FitWidth();
    }

    std::vector<inkpages::KeyedStroke> DocumentView::Load(int page)
    {
        auto bridge = Current();
        return bridge ? bridge->Load(page) : std::vector<inkpages::KeyedStroke>{};
    }

    void DocumentView::Store(int page, std::vector<inkpages::KeyedStroke> strokes)
    {
        std::shared_ptr<PageInkBridge> target;
        {
            std::scoped_lock guard(bridgesLock);
            for (auto const& bridge : bridges)
            {
                if (bridge->IsLive(page))
                {
                    target = bridge;
                    break;
                }
            }
        }
        if (!target)
        {
            LogError(std::format("document view: page {} handed back but no document has it live", page));
            return;
        }
        target->Store(page, strokes);
        // Older documents whose pages are all back are done.
        std::scoped_lock guard(bridgesLock);
        while (bridges.size() > 1 && bridges.front()->LivePages().empty())
        {
            bridges.pop_front();
        }
    }

    void DocumentView::DrawUnderlay(int page, ID2D1DeviceContext* dc, inkpages::RectF area)
    {
        auto bridge = Current();
        if (!bridge || !underlay)
        {
            return;
        }
        std::scoped_lock guard(bridge->Lock());
        auto const& pages = bridge->Document().Pages();
        if (page >= 0 && page < static_cast<int>(pages.size()))
        {
            underlay->Draw(page, *pages[page], dc, area, surface.RenderScale());
        }
    }

    void DocumentView::CaptureInk(std::function<void()> const& work)
    {
        auto bridge = Current();
        if (!bridge)
        {
            return;
        }
        // StrokesOf waits for the ink thread, which takes the document's lock for load/store: collect first, lock after.
        std::map<int, std::vector<inkpages::KeyedStroke>> current;
        for (int page : bridge->LivePages())
        {
            current[page] = surface.StrokesOf(page);
        }
        std::scoped_lock guard(bridge->Lock());
        for (auto const& [page, strokes] : current)
        {
            bridge->Refresh(page, strokes);
        }
        work();
    }
}
