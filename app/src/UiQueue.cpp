#include "UiQueue.h"

#include "inkpages/UiDispatch.h"
#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <format>
#include <memory>
#include <mutex>

namespace xournal::app::UiQueue
{
    namespace
    {
        struct Item
        {
            std::string name;
            std::function<void()> work;
        };

        std::mutex mutex;
        std::shared_ptr<inkpages::UiDispatch> target;

        void RunLogged(Item const& item)
        {
            try
            {
                item.work();
            }
            catch (...)
            {
                LogException(item.name);
            }
        }
    }

    void Attach()
    {
        std::scoped_lock lock(mutex);
        target = std::make_shared<inkpages::UiDispatch>();
    }

    void Detach()
    {
        std::scoped_lock lock(mutex);
        if (target) target->Close();
        target.reset();
    }

    bool Post(std::string name, std::function<void()> work)
    {
        std::shared_ptr<inkpages::UiDispatch> dispatch;
        {
            std::scoped_lock lock(mutex);
            dispatch = target;
        }
        if (!dispatch) return false;
        return dispatch->Post([item = Item{ std::move(name), std::move(work) }] { RunLogged(item); });
    }

    void RunInBackground(std::string name, std::function<void()> work)
    {
        auto item = std::make_unique<Item>(Item{ std::move(name), std::move(work) });
        auto callback = [](PTP_CALLBACK_INSTANCE, void* context)
        {
            std::unique_ptr<Item> owned(static_cast<Item*>(context));
            RunLogged(*owned);
        };
        if (!TrySubmitThreadpoolCallback(callback, item.get(), nullptr))
        {
            ThrowLastError(std::format("TrySubmitThreadpoolCallback ({})", item->name));
        }
        item.release();
    }
}
