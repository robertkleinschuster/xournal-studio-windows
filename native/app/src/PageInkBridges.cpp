#include "PageInkBridges.h"

#include "xournal/base/Log.h"

#include <format>

namespace xournal::app
{
    void PageInkBridges::Add(std::shared_ptr<PageInkBridge> bridge)
    {
        std::scoped_lock guard(lock);
        bridges.push_back(std::move(bridge));
    }

    std::shared_ptr<PageInkBridge> PageInkBridges::Current() const
    {
        std::scoped_lock guard(lock);
        return bridges.empty() ? nullptr : bridges.back();
    }

    std::vector<inkpages::KeyedStroke> PageInkBridges::Load(int page)
    {
        auto bridge = Current();
        return bridge ? bridge->Load(page) : std::vector<inkpages::KeyedStroke>{};
    }

    void PageInkBridges::Store(int page, std::vector<inkpages::KeyedStroke> const& strokes)
    {
        std::shared_ptr<PageInkBridge> target;
        {
            std::scoped_lock guard(lock);
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
            LogError(std::format("ink bridges: page {} handed back but no document has it live", page));
            return;
        }
        target->Store(page, strokes);
        // Older documents whose pages are all back are done.
        std::scoped_lock guard(lock);
        while (bridges.size() > 1 && bridges.front()->LivePages().empty())
        {
            bridges.pop_front();
        }
    }
}
