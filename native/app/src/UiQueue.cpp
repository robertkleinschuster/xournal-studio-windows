#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <format>
#include <memory>

namespace xournal::app::UiQueue
{
    namespace
    {
        HWND target = nullptr;
        UINT queueMessage = 0;

        struct Item
        {
            std::string name;
            std::function<void()> work;
        };

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

    void Attach(HWND window, UINT message)
    {
        target = window;
        queueMessage = message;
    }

    void Post(std::string name, std::function<void()> work)
    {
        auto item = std::make_unique<Item>(Item{ std::move(name), std::move(work) });
        if (!PostMessageW(target, queueMessage, 0, reinterpret_cast<LPARAM>(item.get())))
        {
            // The window is gone (closing): the work cannot run any more - said, not dropped silently.
            LogError(std::format("UiQueue: {} not run: {}", item->name, DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            return;
        }
        item.release(); // owned by the message until Run
    }

    void Run(LPARAM pointer)
    {
        std::unique_ptr<Item> item(reinterpret_cast<Item*>(pointer));
        RunLogged(*item);
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
