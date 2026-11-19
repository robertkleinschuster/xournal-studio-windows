#include "xournal/recovery/Autosave.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <objbase.h>

#include <format>

namespace xournal::recovery
{
    Autosave::Autosave(Recovery recoveryFolder, TakeCopy take, Failed onFailed)
        : recovery(std::move(recoveryFolder)), takeCopy(std::move(take)), failed(std::move(onFailed))
    {
        worker = std::thread([this] { Run(); });
    }

    Autosave::~Autosave()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }

    void Autosave::Report(std::string const& what, std::exception_ptr error) noexcept
    {
        LogException(what, error);
        if (!failed)
        {
            return;
        }
        try
        {
            failed(what, error);
        }
        catch (...)
        {
            LogException("autosave: failed handler");
        }
    }

    bool Autosave::Take(Copy& copy, std::string const& what)
    {
        try
        {
            copy = takeCopy();
            return true;
        }
        catch (...)
        {
            pending = true; // tried again with the next WriteNow
            Report(std::format("snapshot for {}", what), std::current_exception());
            return false;
        }
    }

    void Autosave::WriteNow()
    {
        if (!pending)
        {
            return;
        }
        pending = false;
        std::wstring current = id;
        std::string what = std::format("recovery copy {}", ToUtf8(current));
        Copy copy;
        if (!Take(copy, what))
        {
            return;
        }
        Enqueue("write " + what, [this, current, copy]
        {
            recovery.Write(current, *copy.snapshot, copy.info);
        });
    }

    void Autosave::WriteNowReplacing(std::wstring const& oldId)
    {
        pending = false;
        std::wstring current = id;
        std::string what = std::format("recovery copy {} replacing {}", ToUtf8(current), ToUtf8(oldId));
        Copy copy;
        if (!Take(copy, what + " (the restored copy stays)"))
        {
            return;
        }
        Enqueue("write " + what, [this, current, copy, oldId]
        {
            recovery.Write(current, *copy.snapshot, copy.info);
            recovery.Delete(oldId); // only after a complete write: a failed write throws before
        });
    }

    void Autosave::Discard()
    {
        pending = false;
        std::wstring current = id;
        Enqueue(std::format("delete recovery copy {}", ToUtf8(current)), [this, current] { recovery.Delete(current); });
    }

    void Autosave::StartNewDocument()
    {
        Discard();
        id = Recovery::NewId();
    }

    void Autosave::WaitIdle()
    {
        std::unique_lock lock(mutex);
        idle.wait(lock, [this] { return queue.empty() && !busy; });
    }

    void Autosave::Enqueue(std::string what, Work work)
    {
        {
            std::lock_guard lock(mutex);
            queue.emplace_back(std::move(what), std::move(work));
        }
        wake.notify_one();
    }

    void Autosave::Run() noexcept
    {
        // Windows.Data.Json (the info file) needs an apartment on this thread.
        HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(apartment))
        {
            LogError(std::format("autosave: CoInitializeEx: {} (writing info files will fail)", DescribeHr(apartment)));
        }
        for (;;)
        {
            std::pair<std::string, Work> item;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (queue.empty())
                {
                    break; // stopping, and everything queued is done
                }
                item = std::move(queue.front());
                queue.pop_front();
                busy = true;
            }
            try
            {
                item.second();
            }
            catch (...)
            {
                Report(item.first, std::current_exception());
            }
            {
                std::lock_guard lock(mutex);
                busy = false;
            }
            idle.notify_all();
        }
        if (SUCCEEDED(apartment))
        {
            CoUninitialize();
        }
    }
}
