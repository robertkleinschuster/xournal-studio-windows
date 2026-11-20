#include "PageThumbnails.h"

#include "Bitmaps.h"
#include "ThumbnailDrawing.h"
#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/pdf/DocumentExport.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <vector>

namespace xournal::app
{
    namespace
    {
        constexpr auto Settle = std::chrono::milliseconds(500); // a moment after the last change
        constexpr int ContentPerRound = 8;  // pages drawn per round (the next round follows when they are done)
        constexpr int KeepAround = 40;      // pages this far from the page in view keep their bitmaps

        std::uint64_t ContentKey(std::uint64_t generation, int index)
        {
            // Thumbnail contents beside the screen's renders: bit 29 set.
            return (generation << 32) | 0x20000000u | static_cast<std::uint32_t>(index & 0x0FFFFFFF);
        }
    }

    PageThumbnails::PageThumbnails(DocumentView& view, inkpages::WritingSurface& surface)
        : view(view), surface(surface), refresh("page thumbnails", Settle, [this] { Refresh(); })
    {
    }

    PageThumbnails::Layers PageThumbnails::Of(int index) const
    {
        Layers layers;
        auto doc = view.Document();
        if (!doc || index < 0 || index >= static_cast<int>(doc->Pages().size())) return layers;
        auto const& page = doc->Pages()[index];
        layers.aspect = page->Height() / std::max(1.0, page->Width());
        if (auto it = thumbs.find(page.get()); it != thumbs.end())
        {
            layers.background = it->second.background;
            layers.content = it->second.content;
        }
        return layers;
    }

    void PageThumbnails::SetBox(SIZE size)
    {
        if (size.cx == box.cx && size.cy == box.cy) return;
        box = size;
        Reset(); // rendered at the new size
    }

    void PageThumbnails::SetActive(bool shown)
    {
        active = shown;
        if (active) refresh.Restart();
        else refresh.Cancel();
    }

    void PageThumbnails::Reset()
    {
        ++generation;
        thumbs.clear();
        keep.clear();
        if (active) refresh.Restart();
    }

    void PageThumbnails::Edited(std::set<int> const& pages)
    {
        auto doc = view.Document();
        if (!doc) return;
        for (int index : pages)
        {
            if (index >= 0 && index < static_cast<int>(doc->Pages().size()))
            {
                thumbs[doc->Pages()[index].get()].contentStale = true;
            }
        }
        if (active) refresh.Restart();
    }

    void PageThumbnails::PagesChanged()
    {
        // Pages that left the document are forgotten; new ones have no thumbnail yet.
        auto doc = view.Document();
        std::set<XoppPage const*> present;
        if (doc)
            for (auto const& page : doc->Pages()) present.insert(page.get());
        std::erase_if(thumbs, [&](auto const& entry) { return !present.contains(entry.first); });
        std::erase_if(keep, [&](auto const& entry) { return !present.contains(entry.first); });
        if (active) refresh.Restart();
    }

    int PageThumbnails::IndexOf(XoppPage const* page) const
    {
        auto doc = view.Document();
        if (!doc) return -1;
        auto const& pages = doc->Pages();
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
            if (pages[i].get() == page) return i;
        return -1;
    }

    void PageThumbnails::Refresh()
    {
        auto doc = view.Document();
        if (!active || !doc || box.cx <= 0 || box.cy <= 0) return;
        int count = static_cast<int>(doc->Pages().size());
        int focus = std::clamp(surface.PageInView().value_or(0), 0, std::max(0, count - 1));
        std::vector<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return std::abs(a - focus) < std::abs(b - focus); });

        auto pdf = view.Pdf();
        bool waitForPdf = false;
        std::vector<int> stale;
        for (int index : order)
        {
            if (std::abs(index - focus) > KeepAround) break;
            auto const& page = doc->Pages()[index];
            keep[page.get()] = page;
            Thumb& thumb = thumbs[page.get()];
            if (page->IsPdf() && !thumb.backgroundTried)
            {
                if (pdf && pdf->Loaded()) RequestBackground(index, *page, page);
                else if (pdf && !pdf->Failed()) waitForPdf = true;
            }
            if (thumb.contentStale && !thumb.contentRunning && static_cast<int>(stale.size()) < ContentPerRound)
            {
                stale.push_back(index);
            }
        }
        if (!stale.empty())
        {
            // What lies on the pages as a snapshot would write it - live pages' ink brought in first.
            std::vector<XmlElementPtr> snapshots;
            try
            {
                view.CaptureInk([&]
                {
                    for (int index : stale) snapshots.push_back(doc->Pages()[index]->Snapshot());
                });
            }
            catch (...)
            {
                // The ink could not be read now: the pages stay stale and are tried again in the next round.
                LogException("thumbnails: reading the pages' ink");
                refresh.Restart();
                return;
            }
            for (size_t i = 0; i < stale.size(); ++i)
            {
                RequestContent(stale[i], doc->Pages()[stale[i]], snapshots[i]);
            }
        }
        Trim(focus);
        if (waitForPdf) refresh.Restart(); // the PDF is still loading: its pages come in the next round
    }

    void PageThumbnails::RequestBackground(int index, XoppPage const& page, std::shared_ptr<XoppPage> const& key)
    {
        thumbs[key.get()].backgroundTried = true; // a page the PDF cannot render is not tried again on every change
        SIZE size = ThumbnailSize(box, page.Height() / std::max(1.0, page.Width()));
        std::uint64_t asked = generation;
        XoppPage const* id = key.get();
        view.Pdf()->RenderPage(page.PdfPageIndex(), static_cast<UINT>(size.cx), static_cast<UINT>(size.cy), index,
            [this, alive = std::weak_ptr<bool>(alive), asked, id](std::shared_ptr<CpuBitmap> bitmap)
        {
            if (alive.expired() || asked != generation) return;
            auto it = thumbs.find(id);
            if (it == thumbs.end() || !bitmap) return;
            it->second.background = std::move(bitmap);
            if (updated) updated(IndexOf(id));
        });
    }

    void PageThumbnails::RequestContent(int index, std::shared_ptr<XoppPage> const& page, XmlElementPtr snapshot)
    {
        Thumb& thumb = thumbs[page.get()];
        thumb.contentStale = false;
        thumb.contentRunning = true;
        SIZE size = ThumbnailSize(box, page->Height() / std::max(1.0, page->Width()));
        UINT width = static_cast<UINT>(size.cx), height = static_cast<UINT>(size.cy);
        bool transparent = page->IsPdf();
        auto picture = page->BackgroundImagePath();
        std::uint64_t asked = generation;
        XoppPage const* id = page.get();
        view.Queue().Submit(ContentKey(generation, index), index, std::format("thumbnail of page {}", index + 1),
            [this, alive = std::weak_ptr<bool>(alive), asked, id, snapshot, picture, width, height, transparent](PdfPageRenderer& renderer)
        {
            std::shared_ptr<CpuBitmap> bitmap;
            try
            {
                // This one page, without the background PDF, exported into memory and rendered.
                auto root = XmlElement::Make(L"xournal");
                root->Append(snapshot);
                pdf::DocumentExportSource source{ root, std::nullopt, { picture } };
                pdf::ExportResult result;
                auto pages = pdf::PagesOf(source, result);
                auto bytes = pdf::ExportToMemory(pages, result);
                auto document = Bitmaps::LoadPdf(bytes);
                bitmap = renderer.Render(document.GetPage(0), width, height, transparent);
            }
            catch (...)
            {
                LogException("page thumbnails: content (left as it is until the page is edited)");
            }
            UiQueue::Post("thumbnail content", [this, alive, asked, id, bitmap]
            {
                if (alive.expired() || asked != generation) return;
                auto it = thumbs.find(id);
                if (it == thumbs.end()) return;
                it->second.contentRunning = false;
                if (bitmap) it->second.content = bitmap;
                if (updated) updated(IndexOf(id));
                if (active) refresh.Start(); // the next round of stale pages
            });
        });
    }

    void PageThumbnails::Trim(int focus)
    {
        auto doc = view.Document();
        if (!doc) return;
        auto const& pages = doc->Pages();
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        {
            if (std::abs(i - focus) <= KeepAround) continue;
            auto it = thumbs.find(pages[i].get());
            if (it == thumbs.end() || it->second.contentRunning) continue;
            // Far pages give their bitmaps back and are drawn again when they come near.
            thumbs.erase(it);
            keep.erase(pages[i].get());
        }
    }
}
