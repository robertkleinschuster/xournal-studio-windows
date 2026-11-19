#include "DocumentView.h"

#include "PageUnderlay.h"
#include "xournal/base/Log.h"

#include <format>
#include <map>

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
        surface.load = [bridges = bridges](int page) { return bridges->Load(page); };
        surface.store = [bridges = bridges](int page, std::vector<inkpages::KeyedStroke> strokes) { bridges->Store(page, strokes); };
        surface.drawUnderlay = [this](int page, ID2D1DeviceContext* dc, inkpages::RectF area) { DrawUnderlay(page, dc, area); };
        surface.viewChanged = [this] { OnViewChanged(); };
    }

    DocumentView::~DocumentView()
    {
        // The surface outlives the view: it gets nothing back that points here (load/store hold the bridge set).
        surface.drawUnderlay = nullptr;
        surface.viewChanged = nullptr;
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

    void DocumentView::Show(std::shared_ptr<XoppDocument> doc)
    {
        std::vector<inkpages::PageSpec> specs;
        for (auto const& page : doc->Pages())
        {
            specs.push_back(SpecOf(*page));
        }
        auto bridge = std::make_shared<PageInkBridge>(doc);
        bridges->Add(bridge);
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

    void DocumentView::DrawUnderlay(int page, ID2D1DeviceContext* dc, inkpages::RectF area)
    {
        auto bridge = bridges->Current();
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
        auto bridge = bridges->Current();
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
