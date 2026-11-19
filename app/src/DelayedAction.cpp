#include "DelayedAction.h"

#include "UiQueue.h"

#include "xournal/base/Error.h"

#include <atomic>
#include <format>

namespace xournal::app
{
    struct DelayedAction::Shared : std::enable_shared_from_this<DelayedAction::Shared>
    {
        std::string name;
        std::function<void()> action;
        std::atomic<bool> armed = false;
        bool alive = true; // UI thread only (the destructor and the posted work run there)
    };

    DelayedAction::DelayedAction(std::string actionName, std::chrono::milliseconds wait, std::function<void()> action)
        : name(std::move(actionName)), delay(wait), shared(std::make_shared<Shared>())
    {
        shared->name = name;
        shared->action = std::move(action);
        timer = CreateThreadpoolTimer(Fire, shared.get(), nullptr);
        if (!timer)
        {
            ThrowLastError(std::format("CreateThreadpoolTimer ({})", name));
        }
    }

    DelayedAction::~DelayedAction()
    {
        shared->alive = false;
        SetThreadpoolTimer(timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(timer, TRUE);
        CloseThreadpoolTimer(timer);
    }

    void DelayedAction::Arm()
    {
        shared->armed = true;
        // Relative due time: negative, in 100 ns units.
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(delay.count()) * 10'000;
        FILETIME dueTime{ due.LowPart, static_cast<DWORD>(due.HighPart) };
        SetThreadpoolTimer(timer, &dueTime, 0, 0);
    }

    void DelayedAction::Restart()
    {
        Arm();
    }

    void DelayedAction::Start()
    {
        if (!shared->armed)
        {
            Arm();
        }
    }

    void DelayedAction::Cancel()
    {
        shared->armed = false;
        SetThreadpoolTimer(timer, nullptr, 0, 0);
    }

    void CALLBACK DelayedAction::Fire(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
    {
        auto* state = static_cast<Shared*>(context);
        if (!state->armed.exchange(false))
        {
            return; // cancelled meanwhile
        }
        // The posted work holds the state, so it may run after the owner is gone: then it does nothing.
        UiQueue::Post(state->name, [state = state->shared_from_this()]
        {
            if (state->alive)
            {
                state->action();
            }
        });
    }
}
