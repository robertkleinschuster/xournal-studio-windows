#include "RenderQueue.h"

#include "xournal/base/Log.h"

#include <cstdlib>
#include <format>

namespace xournal::app
{
    RenderQueue::RenderQueue() : thread([this] { Run(); }) {}

    RenderQueue::~RenderQueue()
    {
        Stop();
    }

    void RenderQueue::Submit(std::uint64_t key, int page, std::string name, Job job, int rank)
    {
        {
            std::scoped_lock guard(mutex);
            waiting[key] = { page, rank, std::move(name), std::move(job) };
        }
        wake.notify_one();
    }

    void RenderQueue::Drop(std::function<bool(std::uint64_t key)> const& which)
    {
        std::scoped_lock guard(mutex);
        std::erase_if(waiting, [&](auto const& entry) { return which(entry.first); });
    }

    void RenderQueue::SetFocus(int page)
    {
        std::scoped_lock guard(mutex);
        focus = page;
    }

    void RenderQueue::Stop()
    {
        {
            std::scoped_lock guard(mutex);
            if (stopping && !thread.joinable()) return;
            stopping = true;
            waiting.clear();
        }
        wake.notify_one();
        if (thread.joinable())
        {
            thread.join();
        }
    }

    void RenderQueue::Run()
    {
        // The thread pool's implicit MTA does not cover a thread of our own: WinRT (PdfPage) needs it initialized.
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            LogError(std::format("render thread: CoInitializeEx: {}", DescribeHr(hr)));
        }
        for (;;)
        {
            Waiting next;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !waiting.empty(); });
                if (stopping)
                {
                    break;
                }
                auto nearest = waiting.begin();
                for (auto it = waiting.begin(); it != waiting.end(); ++it)
                {
                    int distance = std::abs(it->second.page - focus), best = std::abs(nearest->second.page - focus);
                    if (distance < best || (distance == best && it->second.rank < nearest->second.rank))
                    {
                        nearest = it;
                    }
                }
                next = std::move(nearest->second);
                waiting.erase(nearest);
            }
            try
            {
                next.job(renderer);
            }
            catch (...)
            {
                LogException(next.name);
            }
        }
        // The device goes while the app still runs (the driver hung when it was released at process exit).
        renderer.Shutdown();
        if (SUCCEEDED(hr))
        {
            CoUninitialize();
        }
    }
}
