#include "DocumentView.h"

#include "PageUnderlay.h"
#include "xournal/base/Log.h"

#include <algorithm>
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
        surface.pagesRemapped = [bridges = bridges](inkpages::PageMap const& map) { bridges->Remapped(map); };
        surface.drawUnderlay = [this](int page, ID2D1DeviceContext* dc, inkpages::RectF area) { DrawUnderlay(page, dc, area); };
        surface.viewChanged = [this] { OnViewChanged(); };
        surface.contentChanged = [this](std::set<int> const& pages) { if (pagesEdited) pagesEdited(pages); };
    }

    DocumentView::~DocumentView()
    {
        // The surface outlives the view: it gets nothing back that points here (load/store hold the bridge set).
        surface.drawUnderlay = nullptr;
        surface.viewChanged = nullptr;
        surface.contentChanged = nullptr;
        surface.SetTextHost(nullptr); // ends an edit while its host is still here
        surface.SetPictureHost(nullptr);
        renderQueue.Stop(); // the PDF device goes while the app still runs
    }

    void DocumentView::MarkCurrentPage()
    {
        // The page in view is outlined only where there is a choice of pages (as the C# PagesController did).
        surface.MarkCurrentPage(PageCount() > 1);
    }

    void DocumentView::OnViewChanged()
    {
        if (underlay)
        {
            underlay->Trim(surface.PageInView().value_or(0));
        }
        if (viewMoved) viewMoved();
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
        surface.SetTextHost(nullptr); // an edit of the old document ends into it, while its host is still here
        surface.SetPictureHost(nullptr);
        texts.reset();
        pictures = std::make_unique<FilePictures>(doc, surface, bridge->Lock(), invalidate);
        texts = std::make_unique<FileTexts>(doc, bridge->Lock(), *pictures, invalidate);
        underlay = std::make_unique<PageUnderlay>(renderQueue, *doc, generation, invalidate, *pictures);
        surface.SetPictureHost(pictures.get());
        surface.SetTextHost(texts.get());
        surface.SetPages(std::move(specs));
        MarkCurrentPage();
        surface.FitWidth();
        if (shown) shown();
    }

    void DocumentView::Remap(inkpages::PageMap const& map, std::function<void(XoppDocument::PageList&)> const& change)
    {
        // The UI side follows here; the ink side (live pages, stroke keys) follows on the ink thread in pagesRemapped.
        auto bridge = bridges->Current();
        {
            std::scoped_lock guard(bridge->Lock());
            change(bridge->Document().Pages());
            bridge->ExpectRearrangementLocked(bridge->Document().Pages());
        }
        pictures->Remap(map);
        underlay->Remap(map);
    }

    void DocumentView::InsertPage(int index, std::shared_ptr<XoppPage> page)
    {
        auto bridge = bridges->Current();
        if (!bridge) return;
        int count = PageCount();
        index = std::clamp(index, 0, count);
        inkpages::PageSpec spec = SpecOf(*page);
        Remap(inkpages::PageMap::Insert(count, index, 1), [&](XoppDocument::PageList& pages)
        {
            pages.insert(pages.begin() + index, std::move(page));
        });
        surface.InsertPages(index, { spec });
        MarkCurrentPage();
        surface.ScrollToPage(index);
        Log(std::format("document view: page inserted at {}", index));
        if (pagesChanged) pagesChanged();
    }

    void DocumentView::InsertPages(int index, std::vector<std::shared_ptr<XoppPage>> pages)
    {
        auto bridge = bridges->Current();
        if (!bridge || pages.empty()) return;
        int count = PageCount();
        index = std::clamp(index, 0, count);
        std::vector<inkpages::PageSpec> specs;
        for (auto const& page : pages) specs.push_back(SpecOf(*page));
        int added = static_cast<int>(pages.size());
        Remap(inkpages::PageMap::Insert(count, index, added), [&](XoppDocument::PageList& list)
        {
            list.insert(list.begin() + index, pages.begin(), pages.end());
        });
        surface.InsertPages(index, std::move(specs));
        MarkCurrentPage();
        surface.ScrollToPage(index);
        Log(std::format("document view: {} pages inserted at {}", added, index));
        if (pagesChanged) pagesChanged();
    }

    void DocumentView::MovePage(int from, int to)
    {
        auto bridge = bridges->Current();
        int count = PageCount();
        if (!bridge || from == to || from < 0 || to < 0 || from >= count || to >= count) return;
        Remap(inkpages::PageMap::Move(count, from, to), [&](XoppDocument::PageList& list)
        {
            auto page = list[from];
            list.erase(list.begin() + from);
            list.insert(list.begin() + to, page);
        });
        surface.MovePage(from, to);
        Log(std::format("document view: page {} moved to {}", from, to));
        if (pagesChanged) pagesChanged();
    }

    std::shared_ptr<XoppDocument> DocumentView::Document() const
    {
        auto bridge = bridges->Current();
        return bridge ? bridge->Shared() : nullptr;
    }

    std::shared_ptr<PdfBackground> DocumentView::Pdf() const
    {
        return underlay ? underlay->Pdf() : nullptr;
    }

    void DocumentView::ReloadPdf()
    {
        auto doc = Document();
        if (!doc || !doc->PdfPath() || !underlay) return;
        underlay->LoadPdf(*doc->PdfPath());
        for (int page = 0; page < PageCount(); ++page)
        {
            surface.InvalidatePage(page);
        }
    }

    bool DocumentView::RemovePage(int index)
    {
        auto bridge = bridges->Current();
        int count = PageCount();
        if (!bridge || count <= 1 || index < 0 || index >= count) return false;
        Remap(inkpages::PageMap::Remove(count, index), [&](XoppDocument::PageList& pages)
        {
            pages.erase(pages.begin() + index);
        });
        surface.RemovePage(index);
        MarkCurrentPage();
        Log(std::format("document view: page {} removed", index));
        if (pagesChanged) pagesChanged();
        return true;
    }

    int DocumentView::PageCount() const
    {
        auto bridge = bridges->Current();
        if (!bridge) return 0;
        std::scoped_lock guard(bridge->Lock());
        return static_cast<int>(bridge->Document().Pages().size());
    }

    std::shared_ptr<XoppPage> DocumentView::PageAt(int index) const
    {
        auto bridge = bridges->Current();
        if (!bridge) return nullptr;
        std::scoped_lock guard(bridge->Lock());
        auto const& pages = bridge->Document().Pages();
        return index >= 0 && index < static_cast<int>(pages.size()) ? pages[index] : nullptr;
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
        // A page that went far meanwhile answers with nothing - its `store` already brought its strokes in. A failed
        // call throws: nothing is refreshed and `work` does not run (the caller reports it; no page is saved half).
        std::map<int, std::vector<inkpages::KeyedStroke>> current;
        for (int page : bridge->LivePages())
        {
            if (auto strokes = surface.StrokesOf(page))
            {
                current[page] = std::move(*strokes);
            }
        }
        std::scoped_lock guard(bridge->Lock());
        for (auto const& [page, strokes] : current)
        {
            bridge->Refresh(page, strokes);
        }
        work();
    }
}
