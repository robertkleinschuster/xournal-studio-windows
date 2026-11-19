#include "PageInkBridge.h"

#include "InkConvert.h"
#include "xournal/base/Log.h"
#include "xournal/model/Strokes.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        Matrix MatrixOf(inkpages::Affine const& a) { return { a.m11, a.m12, a.m21, a.m22, a.dx, a.dy }; }
    }

    std::vector<inkpages::KeyedStroke> PageInkBridge::Load(int page)
    {
        std::scoped_lock guard(mutex);
        auto const& pages = doc->Pages();
        if (page < 0 || page >= static_cast<int>(pages.size()) || live.contains(page))
        {
            LogError(std::format("ink bridge: load of page {} ignored (unknown or already live)", page));
            return {};
        }
        auto held = std::make_unique<HeldInk>();
        std::vector<inkpages::KeyedStroke> strokes;
        std::vector<std::pair<StrokeId, XmlElementPtr>> attached;
        for (auto const& record : pages[page]->Ink().ReadStrokes())
        {
            StrokeId key = nextId++;
            keyed[key] = { record.element, record.element->Value(), page };
            held->ids.push_back(key);
            attached.push_back({ key, record.element });
            strokes.push_back({ InkConvert::ToInkStroke(record.ink), key, {} });
        }
        pages[page]->Ink().Attach(*held, attached);
        live[page] = std::move(held);
        return strokes;
    }

    void PageInkBridge::Hold(int page, HeldInk& held, std::vector<inkpages::KeyedStroke> const& strokes)
    {
        PageInk& pageInk = doc->Pages()[page]->Ink();
        held.ids.clear();
        held.inks.clear();
        for (auto const& s : strokes)
        {
            auto known = s.key != 0 ? keyed.find(static_cast<StrokeId>(s.key)) : keyed.end();
            if (known == keyed.end())
            {
                if (s.key != 0)
                {
                    LogError(std::format("ink bridge: unknown stroke key {} on page {} - written as new ink", s.key, page));
                }
                StrokeId id = nextId++;
                held.ids.push_back(id);
                held.inks[id] = InkConvert::FromInkStroke(s.stroke);
                continue;
            }
            StrokeId key = known->first;
            Keyed const& k = known->second;
            if (k.page == page)
            {
                // The element itself: back to its coordinates as loaded, then moved by the transform since then (exact:
                // never moved twice, never rounded through the ink).
                k.element->SetValue(k.loadedCoordinates);
                if (!s.transform.IsIdentity())
                {
                    Strokes::Transform(*k.element, MatrixOf(s.transform));
                }
                held.ids.push_back(key);
            }
            else
            {
                // Moved here from another page: this page writes a moved copy of the element under a new id; the page it
                // came from no longer has the key, so it leaves the original out.
                auto copy = k.element->Clone();
                copy->SetValue(k.loadedCoordinates);
                Strokes::Transform(*copy, MatrixOf(s.transform));
                StrokeId id = nextId++;
                pageInk.Adopt(id, copy);
                held.ids.push_back(id);
            }
        }
    }

    void PageInkBridge::Store(int page, std::vector<inkpages::KeyedStroke> const& strokes)
    {
        std::scoped_lock guard(mutex);
        auto it = live.find(page);
        if (it == live.end())
        {
            LogError(std::format("ink bridge: store of page {} that was not loaded - its strokes are kept as they are", page));
            return;
        }
        Hold(page, *it->second, strokes);
        doc->Pages()[page]->Ink().Detach();
        live.erase(it);
        // The page's elements are written as they are now: their keys start over at the next load. (A key whose element
        // left the page stays known: another live page holds that stroke and writes its moved copy from it.)
        std::erase_if(keyed, [&](auto const& entry) { return entry.second.page == page && entry.second.element->Parent(); });
    }

    std::set<int> PageInkBridge::LivePages() const
    {
        std::scoped_lock guard(mutex);
        std::set<int> pages;
        for (auto const& [page, _] : live)
        {
            pages.insert(page);
        }
        return pages;
    }

    bool PageInkBridge::IsLive(int page) const
    {
        std::scoped_lock guard(mutex);
        return live.contains(page);
    }

    void PageInkBridge::Refresh(int page, std::vector<inkpages::KeyedStroke> const& strokes)
    {
        auto it = live.find(page);
        if (it != live.end()) // released meanwhile: its XML is current already
        {
            Hold(page, *it->second, strokes);
        }
    }
}
